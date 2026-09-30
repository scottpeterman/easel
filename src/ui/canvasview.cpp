#include "canvasview.h"

#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

using easel::AtlasSlot;
using easel::FramePlan;
using easel::LevelTile;
using easel::TileAtlas;
using easel::TileInstance;
using easel::TilePyramid;
using easel::TileStore;

namespace {

const QColor kBackdrop(0x3a, 0x3a, 0x3a);
constexpr double kZoomStep = 1.25;
constexpr double kRotateStep = 15.0;
constexpr int kCheckerCell = 8;              // logical pixels
constexpr float kCheckerDark = 0.603827f;    // sRGB 0xCC in linear light

// std140 layout of the Frame block in tiles.vert / tiles.frag / outline.vert.
struct FrameUniforms {
    float mvp[16];
    float canvas[4];
    float checker[4];
};
static_assert(sizeof(FrameUniforms) == 96);

// Overlay line vertex: canvas position plus an sRGB colour.
struct OverlayVertex {
    float x, y;
    float r, g, b, a;
};
constexpr double kTwoPi = 6.283185307179586;
constexpr int kCircleSegments = 64;
constexpr int kCanvasOutlineVertices = 5;
constexpr int kRingVertices = kCircleSegments + 1;
constexpr int kOverlayVertices = kCanvasOutlineVertices + 2 * kRingVertices;
constexpr float kOutlineShade = 30.0f / 255.0f;

QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("CanvasView: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

double normalizeDegrees(double d)
{
    d = std::fmod(d, 360.0);
    if (d <= -180.0)
        d += 360.0;
    else if (d > 180.0)
        d -= 360.0;
    return d;
}

} // namespace

CanvasView::CanvasView(QWidget *parent)
    : QRhiWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_TabletTracking); // pen hover moves the brush cursor
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
}

CanvasView::~CanvasView() = default;

void CanvasView::setDocument(TileStore *store, const QSize &canvasSize)
{
    TilePyramid pyramid;
    pyramid.setBase(store, canvasSize);
    setDocument(store, canvasSize, std::move(pyramid));
}

void CanvasView::setDocument(TileStore *store, const QSize &canvasSize, TilePyramid pyramid)
{
    m_store = store;
    m_canvasSize = canvasSize;
    if (m_store)
        m_store->takeDirty();
    if (pyramid.base() != store)
        pyramid.setBase(store, canvasSize);
    m_pyramid = std::move(pyramid);
    resetResidency();
    fitToWindow();
}

void CanvasView::resetResidency()
{
    m_atlas.reset();
    m_defaultSlotDirty = true;
}

qint64 CanvasView::gpuMemoryBytes() const
{
    return qint64(m_pages.size()) * TileAtlas::BytesPerPage;
}

void CanvasView::setPan(const QPointF &pan)
{
    m_pan = pan;
    emitViewChanged();
}

QTransform CanvasView::canvasToView() const
{
    QTransform t;
    t.translate(width() / 2.0 + m_pan.x(), height() / 2.0 + m_pan.y());
    t.rotate(m_rotation);
    t.scale(m_zoom, m_zoom);
    t.translate(-m_canvasSize.width() / 2.0, -m_canvasSize.height() / 2.0);
    return t;
}

QPointF CanvasView::viewToCanvas(const QPointF &viewPos) const
{
    return canvasToView().inverted().map(viewPos);
}

void CanvasView::refresh()
{
    if (!m_store)
        return;
    const auto dirty = m_store->takeDirty();
    if (dirty.isEmpty())
        return;
    for (const LevelTile &t : m_pyramid.invalidate(dirty))
        m_atlas.release(t);
    update();
}

// ---------------------------------------------------------------------------
// GPU resources

void CanvasView::initialize(QRhiCommandBuffer *)
{
    if (m_rhi != rhi()) {
        // New or different QRhi (first show, or moved to another top-level).
        releaseResources();
        m_rhi = rhi();
    }
    if (!m_tilePipeline)
        createPipelines();
}

void CanvasView::createPipelines()
{
    QRhi *r = m_rhi;

    if (!r->isTextureFormatSupported(QRhiTexture::RGBA16F))
        qWarning("CanvasView: RGBA16F textures are not supported by this GPU backend");

    m_uniforms.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                  sizeof(FrameUniforms)));
    m_uniforms->create();

    m_quad.reset(r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 8 * sizeof(float)));
    m_quad->create();

    m_outline.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                 kOverlayVertices * sizeof(OverlayVertex)));
    m_outline->create();

    m_nearest.reset(r->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_nearest->create();
    m_linear.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_linear->create();

    // Page 0 always exists: it carries the default-pixel slot.
    ensurePages(1);

    m_tilePipeline.reset(r->newGraphicsPipeline());
    m_tilePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_tilePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/tiles.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/tiles.frag.qsb"))},
    });
    QRhiVertexInputLayout tileLayout;
    tileLayout.setBindings({
        {2 * sizeof(float)},
        {sizeof(TileInstance), QRhiVertexInputBinding::PerInstance},
    });
    tileLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {1, 1, QRhiVertexInputAttribute::Float4, offsetof(TileInstance, dst)},
        {1, 2, QRhiVertexInputAttribute::Float4, offsetof(TileInstance, uv)},
        {1, 3, QRhiVertexInputAttribute::Float4, offsetof(TileInstance, clamp)},
    });
    m_tilePipeline->setVertexInputLayout(tileLayout);
    m_tilePipeline->setShaderResourceBindings(m_pageBindings.front()[0].get());
    m_tilePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_tilePipeline->create();

    m_outlineBindings.reset(r->newShaderResourceBindings());
    m_outlineBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 m_uniforms.get()),
    });
    m_outlineBindings->create();

    m_outlinePipeline.reset(r->newGraphicsPipeline());
    m_outlinePipeline->setTopology(QRhiGraphicsPipeline::LineStrip);
    m_outlinePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/outline.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/outline.frag.qsb"))},
    });
    QRhiVertexInputLayout outlineLayout;
    outlineLayout.setBindings({{sizeof(OverlayVertex)}});
    outlineLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, offsetof(OverlayVertex, x)},
        {0, 1, QRhiVertexInputAttribute::Float4, offsetof(OverlayVertex, r)},
    });
    m_outlinePipeline->setVertexInputLayout(outlineLayout);
    m_outlinePipeline->setShaderResourceBindings(m_outlineBindings.get());
    m_outlinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_outlinePipeline->create();

    m_quadUploaded = false;
}

void CanvasView::ensurePages(int count)
{
    while (int(m_pages.size()) < count) {
        std::unique_ptr<QRhiTexture> tex(m_rhi->newTexture(
            QRhiTexture::RGBA16F, QSize(TileAtlas::PageSize, TileAtlas::PageSize)));
        tex->create();

        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> srbs;
        QRhiSampler *samplers[2] = {m_nearest.get(), m_linear.get()};
        for (int i = 0; i < 2; ++i) {
            srbs[i].reset(m_rhi->newShaderResourceBindings());
            srbs[i]->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    m_uniforms.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage, tex.get(), samplers[i]),
            });
            srbs[i]->create();
        }
        m_pages.push_back(std::move(tex));
        m_pageBindings.push_back(std::move(srbs));
    }
}

void CanvasView::releaseResources()
{
    m_tilePipeline.reset();
    m_outlinePipeline.reset();
    m_outlineBindings.reset();
    m_pageBindings.clear();
    m_pages.clear();
    m_nearest.reset();
    m_linear.reset();
    m_outline.reset();
    m_instances.reset();
    m_instanceCapacity = 0;
    m_quad.reset();
    m_quadUploaded = false;
    m_uniforms.reset();
    m_rhi = nullptr;
    // Nothing is on the GPU any more.
    resetResidency();
}

void CanvasView::render(QRhiCommandBuffer *cb)
{
    QElapsedTimer timer;
    timer.start();
    ++m_frame;

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    if (!m_quadUploaded) {
        static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
        u->uploadStaticBuffer(m_quad.get(), quad);
        m_quadUploaded = true;
    }

    const QSize px = renderTarget()->pixelSize();
    const QColor clear = kBackdrop;

    if (!m_store || m_canvasSize.isEmpty() || px.isEmpty()) {
        cb->beginPass(renderTarget(), clear, {1.0f, 0}, u);
        cb->endPass();
        return;
    }

    const double dpr = double(px.width()) / double(std::max(1, width()));
    const QTransform xf = canvasToView();
    const QRect canvasRect(QPoint(0, 0), m_canvasSize);
    const QRect visible =
        xf.inverted().mapRect(QRectF(rect())).toAlignedRect().adjusted(-1, -1, 1, 1) & canvasRect;
    const int level = TilePyramid::levelForZoom(m_zoom * dpr, m_pyramid.topLevel());

    const FramePlan plan = easel::planFrame(m_pyramid, m_atlas, visible, level, m_frame, m_uploadBudget);
    ensurePages(m_atlas.pageCount());

    // Default-pixel slot: a whole tile of the store's default, in page 0.
    if (m_defaultSlotDirty) {
        QImage def(TileStore::TileSize, TileStore::TileSize, TileStore::TileFormat);
        const easel::Pixel p = m_store->defaultPixel();
        for (int y = 0; y < def.height(); ++y) {
            auto *line = reinterpret_cast<easel::Pixel *>(def.scanLine(y));
            std::fill(line, line + def.width(), p);
        }
        QRhiTextureSubresourceUploadDescription d(def.constBits(), quint32(def.sizeInBytes()));
        d.setSourceSize(def.size());
        d.setDestinationTopLeft(TileAtlas::slotOrigin(TileAtlas::defaultSlot()));
        u->uploadTexture(m_pages[0].get(), QRhiTextureUploadDescription({0, 0, d}));
        m_defaultSlotDirty = false;
    }

    // Tile uploads, batched per page.
    std::vector<std::vector<QRhiTextureUploadEntry>> perPage(m_pages.size());
    for (const easel::TileUpload &up : plan.uploads) {
        QRhiTextureSubresourceUploadDescription d(up.image.constBits(), quint32(up.image.sizeInBytes()));
        d.setSourceSize(up.image.size());
        d.setDestinationTopLeft(TileAtlas::slotOrigin(up.slot));
        perPage[size_t(up.slot.page)].emplace_back(0, 0, d);
    }
    for (size_t p = 0; p < perPage.size(); ++p) {
        if (perPage[p].empty())
            continue;
        QRhiTextureUploadDescription desc;
        desc.setEntries(perPage[p].cbegin(), perPage[p].cend());
        u->uploadTexture(m_pages[p].get(), desc);
    }

    // Instances, laid out page by page.
    const quint32 bytes = quint32(plan.instanceCount) * sizeof(TileInstance);
    if (bytes > m_instanceCapacity) {
        m_instanceCapacity = std::max<quint32>(bytes, std::max<quint32>(m_instanceCapacity * 2, 64 * 1024));
        m_instances.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, m_instanceCapacity));
        m_instances->create();
    }
    std::vector<quint32> pageOffsets(plan.pages.size(), 0);
    {
        QByteArray data(qsizetype(bytes), Qt::Uninitialized);
        quint32 offset = 0;
        for (qsizetype p = 0; p < plan.pages.size(); ++p) {
            pageOffsets[size_t(p)] = offset;
            const auto &list = plan.pages.at(p);
            const quint32 n = quint32(list.size()) * sizeof(TileInstance);
            if (n)
                std::memcpy(data.data() + offset, list.constData(), n);
            offset += n;
        }
        if (bytes)
            u->updateDynamicBuffer(m_instances.get(), 0, bytes, data.constData());
    }

    // Uniforms.
    QMatrix4x4 proj;
    proj.ortho(0.0f, float(width()), float(height()), 0.0f, -1.0f, 1.0f);
    const QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix() * proj * QMatrix4x4(xf);
    FrameUniforms uni;
    std::memcpy(uni.mvp, mvp.constData(), sizeof(uni.mvp));
    uni.canvas[0] = float(m_canvasSize.width());
    uni.canvas[1] = float(m_canvasSize.height());
    uni.canvas[2] = uni.canvas[3] = 0.0f;
    uni.checker[0] = float(kCheckerCell * dpr);
    uni.checker[1] = 1.0f;
    uni.checker[2] = kCheckerDark;
    uni.checker[3] = 0.0f;
    u->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(uni), &uni);

    // Overlay: canvas outline, then the brush cursor as a light and a dark ring
    // one screen pixel apart, so it shows on any content.
    OverlayVertex overlay[kOverlayVertices];
    {
        const float w = float(m_canvasSize.width()), h = float(m_canvasSize.height());
        const float k = kOutlineShade;
        const float corners[5][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}, {0, 0}};
        for (int i = 0; i < kCanvasOutlineVertices; ++i)
            overlay[i] = {corners[i][0], corners[i][1], k, k, k, 1.0f};
    }
    const bool showCursor = cursorVisible();
    if (showCursor) {
        const double px1 = 1.0 / m_zoom; // one screen pixel in canvas units
        const double inner = std::max(m_tool->cursorDiameter() * 0.5, 2.0 * px1);
        const double radii[2] = {inner + px1, inner};
        const float shade[2] = {1.0f, kOutlineShade};
        for (int ring = 0; ring < 2; ++ring) {
            for (int i = 0; i < kRingVertices; ++i) {
                const double a = kTwoPi * i / kCircleSegments;
                overlay[kCanvasOutlineVertices + ring * kRingVertices + i] = {
                    float(m_cursorCanvas.x() + std::cos(a) * radii[ring]),
                    float(m_cursorCanvas.y() + std::sin(a) * radii[ring]),
                    shade[ring], shade[ring], shade[ring], 1.0f};
            }
        }
    }
    const int overlayCount = showCursor ? kOverlayVertices : kCanvasOutlineVertices;
    u->updateDynamicBuffer(m_outline.get(), 0, quint32(overlayCount * sizeof(OverlayVertex)), overlay);

    // Draw.
    const bool nearest = plan.level == 0 && m_zoom * dpr >= 1.0 - 1e-9;
    cb->beginPass(renderTarget(), clear, {1.0f, 0}, u);
    cb->setViewport({0, 0, float(px.width()), float(px.height())});

    if (plan.instanceCount > 0) {
        cb->setGraphicsPipeline(m_tilePipeline.get());
        for (qsizetype p = 0; p < plan.pages.size(); ++p) {
            const quint32 n = quint32(plan.pages.at(p).size());
            if (n == 0)
                continue;
            cb->setShaderResources(m_pageBindings[size_t(p)][nearest ? 0 : 1].get());
            const QRhiCommandBuffer::VertexInput inputs[] = {
                {m_quad.get(), 0},
                {m_instances.get(), pageOffsets[size_t(p)]},
            };
            cb->setVertexInput(0, 2, inputs);
            cb->draw(4, n);
        }
    }

    cb->setGraphicsPipeline(m_outlinePipeline.get());
    cb->setShaderResources(m_outlineBindings.get());
    const QRhiCommandBuffer::VertexInput outlineInput(m_outline.get(), 0);
    cb->setVertexInput(0, 1, &outlineInput);
    cb->draw(kCanvasOutlineVertices);
    if (showCursor) {
        cb->draw(kRingVertices, 1, kCanvasOutlineVertices);
        cb->draw(kRingVertices, 1, kCanvasOutlineVertices + kRingVertices);
    }

    cb->endPass();

    m_stats.frame = m_frame;
    m_stats.level = plan.level;
    m_stats.instances = plan.instanceCount;
    m_stats.uploads = int(plan.uploads.size());
    m_stats.fallbacks = plan.fallbackCount;
    m_stats.cpuMs = double(timer.nsecsElapsed()) / 1e6;

    // Keep refining until every visible tile is at full resolution for this zoom.
    if (!plan.complete())
        update();
}

// ---------------------------------------------------------------------------
// View control

void CanvasView::zoomBy(double factor, const QPointF &anchor)
{
    const double next = std::clamp(m_zoom * factor, MinZoom, MaxZoom);
    if (qFuzzyCompare(next, m_zoom))
        return;
    const QPointF anchorCanvas = viewToCanvas(anchor);
    m_zoom = next;
    m_pan += anchor - canvasToView().map(anchorCanvas);
    emitViewChanged();
}

void CanvasView::zoomIn()
{
    zoomBy(kZoomStep, QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::zoomOut()
{
    zoomBy(1.0 / kZoomStep, QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::setZoomCentered(double zoom)
{
    zoomBy(zoom / m_zoom, QPointF(width() / 2.0, height() / 2.0));
}

void CanvasView::actualSize()
{
    setZoomCentered(1.0);
}

void CanvasView::fitToWindow()
{
    m_pan = QPointF();
    if (m_canvasSize.isEmpty() || width() <= 0 || height() <= 0) {
        emitViewChanged();
        return;
    }
    QTransform r;
    r.rotate(m_rotation);
    const QSizeF bounds = r.mapRect(QRectF(QPointF(0, 0), QSizeF(m_canvasSize))).size();
    const double margin = 0.92;
    m_zoom = std::clamp(std::min(width() / bounds.width(), height() / bounds.height()) * margin,
                        MinZoom, MaxZoom);
    emitViewChanged();
}

void CanvasView::rotateBy(double degrees)
{
    // Rotate about the view centre: the pan vector rotates with the canvas.
    QTransform r;
    r.rotate(degrees);
    m_pan = r.map(m_pan);
    m_rotation = normalizeDegrees(m_rotation + degrees);
    emitViewChanged();
}

void CanvasView::resetRotation()
{
    rotateBy(-m_rotation);
}

void CanvasView::emitViewChanged()
{
    update();
    emit viewChanged(m_zoom, m_rotation);
}

// ---------------------------------------------------------------------------
// Input

void CanvasView::wheelEvent(QWheelEvent *event)
{
    const double notches = event->angleDelta().y() / 120.0;
    if (notches == 0.0) {
        event->ignore();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier)
        rotateBy(notches * kRotateStep);
    else
        zoomBy(std::pow(kZoomStep, notches), event->position());
    event->accept();
}

void CanvasView::setTool(CanvasTool *tool)
{
    if (m_stroking)
        endStroke(m_lastSample);
    m_tool = tool;
    updateCursor();
    update();
}

bool CanvasView::cursorVisible() const
{
    return m_tool && m_hovering && !m_spaceHeld && !m_panning && m_tool->cursorDiameter() > 0.0;
}

easel::StrokeSample CanvasView::sampleAt(const QPointF &viewPos, double pressure) const
{
    return {viewToCanvas(viewPos), pressure};
}

void CanvasView::beginStroke(const easel::StrokeSample &s)
{
    if (!m_tool)
        return;
    m_stroking = true;
    m_lastSample = s;
    m_tool->press(s);
    refresh();
}

void CanvasView::continueStroke(const easel::StrokeSample &s)
{
    m_lastSample = s;
    m_tool->move(s);
    refresh();
}

void CanvasView::endStroke(const easel::StrokeSample &s)
{
    m_stroking = false;
    m_tool->release(s);
    refresh();
    emit strokeFinished();
}

void CanvasView::trackCursor(const QPointF &viewPos)
{
    m_cursorCanvas = viewToCanvas(viewPos);
    m_hovering = true;
    const bool inside = m_cursorCanvas.x() >= 0 && m_cursorCanvas.y() >= 0
                        && m_cursorCanvas.x() < m_canvasSize.width()
                        && m_cursorCanvas.y() < m_canvasSize.height();
    emit cursorMoved(m_cursorCanvas, inside);
    if (m_tool)
        update();
}

void CanvasView::startPan(const QPointF &viewPos)
{
    m_panning = true;
    m_lastPos = viewPos;
    updateCursor();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    const bool panButton = event->button() == Qt::MiddleButton
                           || (event->button() == Qt::LeftButton && m_spaceHeld);
    if (panButton) {
        startPan(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_tool && !m_stroking) {
        beginStroke(sampleAt(event->position(), 1.0));
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_pan += event->position() - m_lastPos;
        m_lastPos = event->position();
        update();
    } else if (m_stroking) {
        continueStroke(sampleAt(event->position(), 1.0));
    }
    trackCursor(event->position());
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        updateCursor();
        event->accept();
        return;
    }
    if (m_stroking && event->button() == Qt::LeftButton) {
        endStroke(sampleAt(event->position(), 1.0));
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void CanvasView::tabletEvent(QTabletEvent *event)
{
    // Accepted tablet events are not re-sent as mouse events, so pen input
    // arrives here once, with pressure.
    const QPointF pos = event->position();
    const double pressure = std::clamp(double(event->pressure()), 0.0, 1.0);

    switch (event->type()) {
    case QEvent::TabletPress:
        if (event->button() == Qt::MiddleButton || m_spaceHeld)
            startPan(pos);
        else if (event->button() == Qt::LeftButton && m_tool && !m_stroking)
            beginStroke({viewToCanvas(pos), pressure});
        break;
    case QEvent::TabletMove:
        if (m_panning) {
            m_pan += pos - m_lastPos;
            m_lastPos = pos;
            update();
        } else if (m_stroking) {
            continueStroke({viewToCanvas(pos), pressure});
        }
        trackCursor(pos);
        break;
    case QEvent::TabletRelease:
        if (m_panning) {
            m_panning = false;
            updateCursor();
        } else if (m_stroking) {
            // Release reports zero pressure; finish at the last real one.
            endStroke({viewToCanvas(pos), m_lastSample.pressure});
        }
        break;
    default:
        break;
    }
    event->accept();
}

void CanvasView::leaveEvent(QEvent *event)
{
    m_hovering = false;
    update();
    QRhiWidget::leaveEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        update();
        event->accept();
        return;
    }
    QRhiWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        m_panning = false;
        updateCursor();
        update();
        event->accept();
        return;
    }
    QRhiWidget::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent *event)
{
    m_spaceHeld = false;
    m_panning = false;
    if (m_stroking)
        endStroke(m_lastSample);
    updateCursor();
    QRhiWidget::focusOutEvent(event);
}

void CanvasView::updateCursor()
{
    if (m_panning)
        setCursor(Qt::ClosedHandCursor);
    else if (m_spaceHeld)
        setCursor(Qt::OpenHandCursor);
    else if (m_tool)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
}
