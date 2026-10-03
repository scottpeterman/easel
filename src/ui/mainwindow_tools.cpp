// MainWindow: free transform, flips and quarter turns; fill and gradient; filters.

#include "mainwindow.h"

#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "edittools.h"
#include "fillops.h"
#include "filterdialog.h"
#include "filters.h"
#include "gradienteditor.h"
#include "selecttools.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <climits>
#include <cmath>
#include <functional>

using easeletch::FreeTransform;
using easeletch::TransformHandle;

namespace {

// Previews fall back to hard pixels while dragging once a smooth redraw takes
// longer than this; the full-quality one happens when the drag ends.
constexpr qint64 kSmoothPreviewBudgetMs = 40;

} // namespace

void MainWindow::createTransformOptions()
{
    m_transformOptions = new QToolBar(tr("Transform Options"), this);
    m_transformOptions->setObjectName(QStringLiteral("TransformOptionsBar"));
    m_transformOptions->setMovable(false);

    auto *host = new QWidget(m_transformOptions);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto *title = new QLabel(tr("Transform"), host);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(title);
    row->addSpacing(8);

    const auto percentBox = [host] {
        auto *box = new QDoubleSpinBox(host);
        box->setRange(-6400.0, 6400.0);
        box->setDecimals(1);
        box->setSuffix(tr("%"));
        box->setKeyboardTracking(false);
        box->setToolTip(tr("100% is the size it was. A negative size flips it."));
        return box;
    };
    row->addWidget(new QLabel(tr("W"), host));
    m_xfWidth = percentBox();
    row->addWidget(m_xfWidth);
    row->addWidget(new QLabel(tr("H"), host));
    m_xfHeight = percentBox();
    row->addWidget(m_xfHeight);
    auto *link = new QCheckBox(tr("Linked"), host);
    link->setChecked(true);
    link->setToolTip(tr("Changing the width or the height changes the other to match"));
    row->addWidget(link);

    row->addSpacing(8);
    row->addWidget(new QLabel(tr("Angle"), host));
    m_xfAngle = new QDoubleSpinBox(host);
    m_xfAngle->setRange(-360.0, 360.0);
    m_xfAngle->setDecimals(1);
    m_xfAngle->setSuffix(tr("°"));
    m_xfAngle->setKeyboardTracking(false);
    row->addWidget(m_xfAngle);

    m_xfSmooth = new QCheckBox(tr("Smooth"), host);
    m_xfSmooth->setToolTip(tr("Blend pixels for smooth edges. Untick to keep hard pixels (sprites, pixel art)."));
    m_xfSmooth->setChecked(m_transformSmooth);
    row->addSpacing(8);
    row->addWidget(m_xfSmooth);

    const auto button = [this, host, row](const QString &text, const QString &tip, auto slot) {
        auto *b = new QToolButton(host);
        b->setText(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };
    row->addSpacing(8);
    button(tr("Flip H"), tr("Flip left to right"), &MainWindow::flipHorizontal);
    button(tr("Flip V"), tr("Flip top to bottom"), &MainWindow::flipVertical);
    button(tr("Turn 90°"), tr("Rotate a quarter turn clockwise"), [this] { rotateQuarter(1); });
    row->addSpacing(12);
    button(tr("Apply"), tr("Apply the transform (Enter)"), &MainWindow::commitFloating);
    button(tr("Cancel"), tr("Put everything back (Escape)"), &MainWindow::cancelFloating);
    row->addStretch(1);
    m_transformOptions->addWidget(host);

    // A linked edit keeps the shape: the other side changes by the same factor.
    connect(m_xfWidth, &QDoubleSpinBox::valueChanged, this, [this, link](double v) {
        if (!m_xf.active || v == 0.0)
            return;
        const double sx = v / 100.0;
        const double sy = link->isChecked() && m_xf.box.scaleX != 0.0
                              ? m_xf.box.scaleY * std::abs(sx / m_xf.box.scaleX)
                              : m_xf.box.scaleY;
        setTransform(sx, sy, m_xf.box.angle);
    });
    connect(m_xfHeight, &QDoubleSpinBox::valueChanged, this, [this, link](double v) {
        if (!m_xf.active || v == 0.0)
            return;
        const double sy = v / 100.0;
        const double sx = link->isChecked() && m_xf.box.scaleY != 0.0
                              ? m_xf.box.scaleX * std::abs(sy / m_xf.box.scaleY)
                              : m_xf.box.scaleX;
        setTransform(sx, sy, m_xf.box.angle);
    });
    connect(m_xfAngle, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_xf.active)
            setTransform(m_xf.box.scaleX, m_xf.box.scaleY, v);
    });
    connect(m_xfSmooth, &QCheckBox::toggled, this, &MainWindow::setTransformSmooth);

    addToolBar(Qt::TopToolBarArea, m_transformOptions);
}

bool MainWindow::beginTransform()
{
    return startTransform(true);
}

bool MainWindow::startTransform(bool interactive)
{
    if (!m_stack || m_view->isStroking())
        return false;
    if (m_xf.active)
        return true;
    if (m_text.active)
        commitFloating(); // text being typed is placed first, then it's the layer that's transformed
    // Pixels already floating (a paste being placed) are transformed as they are.
    const bool wasFloating = m_floating.isActive();
    const bool hadSelection = !m_selection.isEmpty();
    CanvasTool *previous = m_view->tool();
    if (!liftForMove())
        return false;
    if (!wasFloating)
        m_floatLabel = tr("Transform");

    m_xf = TransformSession();
    m_xf.active = true;
    m_xf.source = m_floating.content();
    m_xf.shape = m_floating.selection().translated(-m_floating.position());
    m_xf.hadSelection = hadSelection || wasFloating;
    m_xf.box.size = m_xf.source.size();
    m_xf.box.center = QPointF(m_floating.position())
                      + QPointF(m_xf.source.width() / 2.0, m_xf.source.height() / 2.0);
    if (!interactive)
        return true;

    m_xf.previousTool = previous == m_transformTool ? m_move : previous;
    activateTool(m_transformTool, false);
    updateTransformOutline();
    syncTransformOptions();
    statusBar()->showMessage(tr("Drag a corner to scale (Shift: any shape), outside the box to rotate "
                                "(Shift: 15° steps), inside it to move. Enter applies, Escape cancels."),
                             10000);
    return true;
}

void MainWindow::endTransform()
{
    if (!m_xf.active)
        return;
    CanvasTool *previous = m_xf.previousTool;
    m_xf = TransformSession();
    m_view->setHandles({});
    syncTransformOptions();
    if (previous && m_view->tool() == m_transformTool)
        activateTool(previous, previous == m_brush);
}

void MainWindow::applyTransform(bool final)
{
    if (!m_xf.active || !m_floating.isActive())
        return;
    if (m_xf.box.isIdentity()) {
        const QPointF corner = m_xf.box.center
                               - QPointF(m_xf.source.width() / 2.0, m_xf.source.height() / 2.0);
        m_floating.replace(m_xf.source, m_xf.shape,
                           QPoint(int(std::lround(corner.x())), int(std::lround(corner.y()))));
    } else {
        const QTransform matrix = m_xf.box.matrix();
        const bool smooth = m_transformSmooth && (final || !m_xf.changed || m_lastSmoothMs <= kSmoothPreviewBudgetMs);
        QElapsedTimer timer;
        timer.start();
        QPoint origin;
        const QImage image = easeletch::transformImage(m_xf.source, matrix, smooth, &origin, canvasRect());
        if (smooth)
            m_lastSmoothMs = timer.elapsed();
        m_floating.replace(image, easeletch::transformShape(m_xf.shape, m_xf.source.size(), matrix, canvasRect()),
                           origin);
    }
    m_xf.changed = true;
    m_floatStart = QPoint(INT_MIN, INT_MIN); // committing records a step even if it ends where it began
    m_selection = m_floating.selection();
    updateSelectionActions();
    if (m_view->tool() == m_transformTool)
        updateTransformOutline();
    syncTransformOptions();
    m_view->refresh();
}

void MainWindow::updateTransformOutline()
{
    if (!m_xf.active)
        return;
    // The box, and a small square on each handle: eight screen pixels wide,
    // whatever the zoom.
    const FreeTransform &box = m_xf.box;
    QPolygonF frame = box.corners();
    frame << frame.first();
    QList<QPolygonF> handles;

    const double half = 4.0 / qMax(m_view->zoom(), 0.01);
    const QTransform turn = QTransform().rotate(box.angle);
    const bool small = std::abs(box.size.width() * box.scaleX) < half * 8.0
                       || std::abs(box.size.height() * box.scaleY) < half * 8.0;
    const auto square = [&](TransformHandle h) {
        const QPointF c = easeletch::handlePosition(box, h);
        QPolygonF s;
        for (const QPointF &d : {QPointF(-half, -half), QPointF(half, -half), QPointF(half, half),
                                 QPointF(-half, half)})
            s << c + turn.map(d);
        handles << s;
    };
    for (const TransformHandle h : {TransformHandle::TopLeft, TransformHandle::TopRight,
                                    TransformHandle::BottomRight, TransformHandle::BottomLeft})
        square(h);
    if (!small) {
        for (const TransformHandle h : {TransformHandle::Top, TransformHandle::Right, TransformHandle::Bottom,
                                        TransformHandle::Left})
            square(h);
    }
    m_view->setSelectionOutline({frame});
    m_view->setHandles(handles);
}

void MainWindow::syncTransformOptions()
{
    if (!m_xfWidth)
        return;
    const QSignalBlocker b1(m_xfWidth), b2(m_xfHeight), b3(m_xfAngle), b4(m_xfSmooth);
    m_xfWidth->setValue(m_xf.active ? m_xf.box.scaleX * 100.0 : 100.0);
    m_xfHeight->setValue(m_xf.active ? m_xf.box.scaleY * 100.0 : 100.0);
    m_xfAngle->setValue(m_xf.active ? m_xf.box.angle : 0.0);
    m_xfSmooth->setChecked(m_transformSmooth);
}

void MainWindow::setTransform(double scaleX, double scaleY, double angle)
{
    if (!m_xf.active || scaleX == 0.0 || scaleY == 0.0)
        return;
    m_xf.box.scaleX = scaleX;
    m_xf.box.scaleY = scaleY;
    m_xf.box.angle = 0.0;
    m_xf.box.rotate(angle);
    applyTransform(true);
}

void MainWindow::setTransformSmooth(bool smooth)
{
    if (m_transformSmooth == smooth)
        return;
    m_transformSmooth = smooth;
    if (m_xf.active && m_xf.changed)
        applyTransform(true);
    else
        syncTransformOptions();
}

void MainWindow::quickTransform(const QString &label, void (FreeTransform::*change)())
{
    if (!m_stack || m_view->isStroking())
        return;
    if (m_xf.active) {
        (m_xf.box.*change)();
        applyTransform(true);
        return;
    }
    const bool wasFloating = m_floating.isActive() && !m_text.active;
    if (!startTransform(false))
        return;
    (m_xf.box.*change)();
    applyTransform(true);
    if (wasFloating) {
        // A paste still being placed stays floating, now flipped or turned.
        m_xf = TransformSession();
        showFloatingOutline();
        m_view->refresh();
    } else {
        m_floatLabel = label;
        commitFloating();
    }
}

void MainWindow::flipHorizontal()
{
    quickTransform(tr("Flip Horizontal"), &FreeTransform::flipHorizontal);
}

void MainWindow::flipVertical()
{
    quickTransform(tr("Flip Vertical"), &FreeTransform::flipVertical);
}

void MainWindow::rotateQuarter(int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 1)
        quickTransform(tr("Rotate 90° Right"), &FreeTransform::quarterRight);
    else if (turns == 3)
        quickTransform(tr("Rotate 90° Left"), &FreeTransform::quarterLeft);
    else if (turns == 2)
        quickTransform(tr("Rotate 180°"), &FreeTransform::halfTurn);
}

void MainWindow::transformPressed(const QPointF &pos)
{
    if (!m_xf.active)
        return;
    m_xf.handle = easeletch::hitTest(m_xf.box, pos, 8.0 / qMax(m_view->zoom(), 0.01));
    m_xf.start = m_xf.box;
    m_xf.dragFrom = pos;
    m_xf.dragging = true;
}

void MainWindow::transformDragged(const QPointF &pos)
{
    if (!m_xf.active || !m_xf.dragging)
        return;
    const bool shift = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    m_xf.box = easeletch::dragHandle(m_xf.start, m_xf.handle, m_xf.dragFrom, pos, shift);
    applyTransform(false);
}

void MainWindow::transformReleased(const QPointF &pos)
{
    if (!m_xf.active || !m_xf.dragging)
        return;
    const bool shift = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    m_xf.box = easeletch::dragHandle(m_xf.start, m_xf.handle, m_xf.dragFrom, pos, shift);
    m_xf.dragging = false;
    // A click that moved nothing isn't a change.
    if (m_xf.changed || !(m_xf.box.isIdentity() && m_xf.box.center == m_xf.start.center))
        applyTransform(true);
}

// --- Fill and gradient ------------------------------------------------------------

void MainWindow::createFillOptions()
{
    m_fillOptions = new QToolBar(tr("Fill Options"), this);
    m_fillOptions->setObjectName(QStringLiteral("FillOptionsBar"));
    m_fillOptions->setMovable(false);

    auto *host = new QWidget(m_fillOptions);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto *title = new QLabel(tr("Fill"), host);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(title);
    row->addSpacing(8);
    row->addWidget(new QLabel(tr("Tolerance"), host));
    auto *slider = new QSlider(Qt::Horizontal, host);
    slider->setRange(0, 100);
    slider->setFixedWidth(120);
    m_fillTolerance = new QSpinBox(host);
    m_fillTolerance->setRange(0, 100);
    m_fillTolerance->setSuffix(tr("%"));
    m_fillTolerance->setKeyboardTracking(false);
    m_fillTolerance->setToolTip(tr("How different a colour can be and still be filled. Raise it to reach "
                                   "into soft edges."));
    m_fillContiguous = new QCheckBox(tr("Contiguous"), host);
    m_fillContiguous->setToolTip(tr("Only the area connected to the pixel clicked; off fills that colour everywhere"));
    m_fillAllLayers = new QCheckBox(tr("All layers"), host);
    m_fillAllLayers->setToolTip(tr("Find the area in the whole picture, not just this layer: "
                                   "fill under line art that's on another layer"));
    auto *hint = new QLabel(tr("Fills with the current colour, inside the selection"), host);
    hint->setEnabled(false);
    row->addWidget(slider);
    row->addWidget(m_fillTolerance);
    row->addWidget(m_fillContiguous);
    row->addWidget(m_fillAllLayers);
    row->addSpacing(12);
    row->addWidget(hint);
    row->addStretch(1);
    m_fillOptions->addWidget(host);

    connect(slider, &QSlider::valueChanged, m_fillTolerance, &QSpinBox::setValue);
    connect(m_fillTolerance, &QSpinBox::valueChanged, this, [this, slider](int v) {
        const QSignalBlocker block(slider);
        slider->setValue(v);
        m_fill.tolerance = v / 100.0;
    });
    connect(m_fillContiguous, &QCheckBox::toggled, this, [this](bool on) { m_fill.contiguous = on; });
    connect(m_fillAllLayers, &QCheckBox::toggled, this, [this](bool on) { m_fill.allLayers = on; });
    addToolBar(Qt::TopToolBarArea, m_fillOptions);
    syncFillOptions();
}

void MainWindow::syncFillOptions()
{
    if (!m_fillTolerance)
        return;
    const FillOptions o = m_fill; // the widgets write back as they're set
    m_fillTolerance->setValue(int(std::lround(o.tolerance * 100.0)));
    m_fillContiguous->setChecked(o.contiguous);
    m_fillAllLayers->setChecked(o.allLayers);
    m_fill = o;
}

void MainWindow::setFillOptions(const FillOptions &options)
{
    m_fill = options;
    m_fill.tolerance = std::clamp(m_fill.tolerance, 0.0, 1.0);
    syncFillOptions();
}

namespace {

const QSize kGradientSwatch(56, 14);
const QString kColourTransparent = QStringLiteral("colour-transparent");
const QString kColourEnd = QStringLiteral("colour-end");
const QString kColourShaded = QStringLiteral("colour-shaded");
const QString kCustom = QStringLiteral("custom");
const QString kUserPrefix = QStringLiteral("user:");

} // namespace

void MainWindow::createGradientOptions()
{
    m_gradientOptions = new QToolBar(tr("Gradient Options"), this);
    m_gradientOptions->setObjectName(QStringLiteral("GradientOptionsBar"));
    m_gradientOptions->setMovable(false);

    auto *host = new QWidget(m_gradientOptions);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto *title = new QLabel(tr("Gradient"), host);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(title);
    row->addSpacing(8);

    m_gradientPreset = new QComboBox(host);
    m_gradientPreset->setIconSize(kGradientSwatch);
    m_gradientPreset->setMaxVisibleItems(24);
    m_gradientPreset->setToolTip(tr("The colours. The first three use the current colour; "
                                    "the metals are light and dark bands that read as reflections."));
    row->addWidget(m_gradientPreset);
    auto *edit = new QToolButton(host);
    edit->setText(tr("Edit…"));
    edit->setFocusPolicy(Qt::NoFocus);
    edit->setToolTip(tr("Change this gradient's colours, or build your own and save it"));
    row->addWidget(edit);
    m_gradientRemove = new QToolButton(host);
    m_gradientRemove->setText(tr("Remove"));
    m_gradientRemove->setFocusPolicy(Qt::NoFocus);
    m_gradientRemove->setToolTip(tr("Remove this saved gradient from the list"));
    row->addWidget(m_gradientRemove);

    row->addSpacing(8);
    m_gradientShape = new QComboBox(host);
    m_gradientShape->addItems({tr("Linear"), tr("Radial"), tr("Reflected"), tr("Conical")});
    m_gradientShape->setItemData(0, tr("Along the line you drag"), Qt::ToolTipRole);
    m_gradientShape->setItemData(1, tr("Outward from where you start: with a highlight, a ball"), Qt::ToolTipRole);
    m_gradientShape->setItemData(2, tr("Along the line and mirrored behind it: a band, a rod or pipe"),
                                 Qt::ToolTipRole);
    m_gradientShape->setItemData(3, tr("Swept round where you start: a disc, a knob, a cone from above"),
                                 Qt::ToolTipRole);
    m_gradientShape->setToolTip(tr("How the colours spread from the line you drag"));
    row->addWidget(m_gradientShape);

    m_gradientEnd = new QToolButton(host);
    m_gradientEnd->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_gradientEnd->setText(tr("End colour"));
    m_gradientEnd->setFocusPolicy(Qt::NoFocus);
    m_gradientEnd->setToolTip(tr("For \"Colour to end colour\": the colour it ends on. Click to set it to the "
                                 "current colour, then pick the colour it starts with."));
    row->addWidget(m_gradientEnd);
    m_gradientReverse = new QCheckBox(tr("Reverse"), host);
    m_gradientReverse->setToolTip(tr("Swap the two ends"));
    row->addWidget(m_gradientReverse);
    row->addStretch(1);
    m_gradientOptions->addWidget(host);

    connect(m_gradientPreset, &QComboBox::activated, this, [this](int i) {
        m_gradient.preset = m_gradientPreset->itemData(i).toString();
        syncGradientOptions();
    });
    connect(edit, &QToolButton::clicked, this, &MainWindow::editGradient);
    connect(m_gradientRemove, &QToolButton::clicked, this, [this] { removeUserGradient(m_gradient.preset); });
    connect(m_gradientShape, &QComboBox::activated, this,
            [this](int i) { m_gradient.shape = easeletch::GradientShape(i); });
    connect(m_gradientReverse, &QCheckBox::toggled, this, [this](bool on) { m_gradient.reverse = on; });
    connect(m_gradientEnd, &QToolButton::clicked, this, [this] {
        m_gradient.end = m_color->color();
        m_gradient.preset = kColourEnd;
        rebuildGradientPresets();
    });
    // The first three follow the painting colour.
    connect(m_color, &ColorPanel::colorChanged, this, [this] {
        for (int i = 0; i < m_gradientPreset->count(); ++i) {
            const QString id = m_gradientPreset->itemData(i).toString();
            if (id.startsWith(QLatin1String("colour-")))
                m_gradientPreset->setItemIcon(i, GradientBar::swatch(presetStops(id), kGradientSwatch));
        }
    });
    addToolBar(Qt::TopToolBarArea, m_gradientOptions);
    rebuildGradientPresets();
}

easeletch::GradientStops MainWindow::presetStops(const QString &id) const
{
    const QColor colour = m_color ? m_color->color() : QColor(Qt::black);
    if (id == kColourEnd)
        return easeletch::twoStops(colour, m_gradient.end);
    if (id == kColourShaded)
        return easeletch::shadedStops(colour);
    if (id == kCustom && m_gradient.custom.size() >= 2)
        return m_gradient.custom;
    for (const easeletch::GradientPreset &g : easeletch::gradientPresets())
        if (g.id == id)
            return g.stops;
    for (const easeletch::GradientPreset &g : m_userGradients)
        if (g.id == id)
            return g.stops;
    return easeletch::twoStops(colour, Qt::transparent);
}

easeletch::GradientStops MainWindow::currentGradientStops() const
{
    const easeletch::GradientStops stops = presetStops(m_gradient.preset);
    return m_gradient.reverse ? easeletch::reversedStops(stops) : stops;
}

void MainWindow::rebuildGradientPresets()
{
    if (!m_gradientPreset)
        return;
    const QSignalBlocker block(m_gradientPreset);
    m_gradientPreset->clear();
    const auto add = [this](const QString &id, const QString &name) {
        m_gradientPreset->addItem(GradientBar::swatch(presetStops(id), kGradientSwatch), name, id);
    };
    add(kColourTransparent, tr("Colour to transparent"));
    add(kColourEnd, tr("Colour to end colour"));
    add(kColourShaded, tr("Colour, shaded"));
    m_gradientPreset->insertSeparator(m_gradientPreset->count());
    for (const easeletch::GradientPreset &g : easeletch::gradientPresets())
        add(g.id, g.name);
    if (!m_userGradients.isEmpty() || m_gradient.custom.size() >= 2)
        m_gradientPreset->insertSeparator(m_gradientPreset->count());
    for (const easeletch::GradientPreset &g : std::as_const(m_userGradients))
        add(g.id, g.name);
    if (m_gradient.custom.size() >= 2)
        add(kCustom, tr("Custom"));
    syncGradientOptions();
}

void MainWindow::syncGradientOptions()
{
    if (!m_gradientShape)
        return;
    int at = m_gradientPreset->findData(m_gradient.preset);
    if (at < 0) {
        // A preset that's gone (removed, or from other settings).
        m_gradient.preset = kColourTransparent;
        at = m_gradientPreset->findData(m_gradient.preset);
    }
    {
        const QSignalBlocker b1(m_gradientPreset), b2(m_gradientShape), b3(m_gradientReverse);
        m_gradientPreset->setCurrentIndex(at);
        m_gradientShape->setCurrentIndex(int(m_gradient.shape));
        m_gradientReverse->setChecked(m_gradient.reverse);
    }
    m_gradientEnd->setEnabled(m_gradient.preset == kColourEnd);
    m_gradientRemove->setVisible(m_gradient.preset.startsWith(kUserPrefix));
    QPixmap swatch(14, 14);
    swatch.fill(m_gradient.end);
    m_gradientEnd->setIcon(swatch);
}

void MainWindow::setGradientOptions(const GradientOptions &options)
{
    m_gradient = options;
    if (!m_gradient.end.isValid())
        m_gradient.end = Qt::white;
    if (m_gradient.custom.size() >= 2)
        m_gradient.custom = easeletch::normalizedStops(m_gradient.custom);
    rebuildGradientPresets();
}

QString MainWindow::saveUserGradient(const QString &name, const easeletch::GradientStops &stops)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty() || stops.size() < 2)
        return {};
    easeletch::GradientPreset g;
    g.id = kUserPrefix + trimmed;
    g.name = trimmed;
    g.stops = easeletch::normalizedStops(stops);
    bool replaced = false;
    for (easeletch::GradientPreset &old : m_userGradients) {
        if (old.id == g.id) {
            old = g;
            replaced = true;
        }
    }
    if (!replaced)
        m_userGradients.append(g);
    rebuildGradientPresets();
    return g.id;
}

bool MainWindow::removeUserGradient(const QString &id)
{
    const qsizetype removed =
        m_userGradients.removeIf([&id](const easeletch::GradientPreset &g) { return g.id == id; });
    if (removed == 0)
        return false;
    rebuildGradientPresets(); // the selection falls back if it was this one
    return true;
}

void MainWindow::editGradient()
{
    GradientDialog dlg(presetStops(m_gradient.preset), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!dlg.presetName().isEmpty()) {
        m_gradient.preset = saveUserGradient(dlg.presetName(), dlg.stops());
        statusBar()->showMessage(tr("Saved gradient \"%1\"").arg(dlg.presetName()), 4000);
    } else {
        m_gradient.custom = dlg.stops();
        m_gradient.preset = kCustom;
    }
    rebuildGradientPresets();
}

void MainWindow::fillAt(const QPoint &pos)
{
    if (!m_stack) // called from the tool's press, so a stroke is under way
        return;
    commitFloating();
    easeletch::TileStore *store = editStore();
    if (!store || !canvasRect().contains(pos))
        return;
    if (!m_selection.isEmpty() && !m_selection.contains(pos.x(), pos.y())) {
        statusBar()->showMessage(tr("That's outside the selection: Ctrl+D deselects"), 4000);
        return;
    }
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const easeletch::TileStore *look = store;
    if (m_fill.allLayers && !m_editMask) {
        syncComposite();
        look = &m_stack->composite();
    }
    const easeletch::Selection area =
        easeletch::magicWand(*look, canvasRect(), pos, m_fill.tolerance, m_fill.contiguous);
    auto before = easeletch::fillRegion(*store, area, m_selection, canvasRect(), m_color->color());
    QApplication::restoreOverrideCursor();
    if (before.isEmpty())
        return; // already that colour
    m_history.push(tr("Fill"), m_stack->activeId(), std::move(before), m_editMask);
    m_color->noteUsed(m_color->color());
    m_view->refresh();
    historyChanged();
}

void MainWindow::fillSelection()
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    easeletch::TileStore *store = editStore();
    if (!store)
        return;
    const easeletch::Selection area = m_selection.isEmpty() ? easeletch::Selection::rect(canvasRect()) : m_selection;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    auto before = easeletch::fillRegion(*store, area, {}, canvasRect(), m_color->color());
    QApplication::restoreOverrideCursor();
    if (before.isEmpty())
        return;
    m_history.push(tr("Fill"), m_stack->activeId(), std::move(before), m_editMask);
    m_color->noteUsed(m_color->color());
    m_view->refresh();
    historyChanged();
}

void MainWindow::drawGradient(const QPointF &from, const QPointF &to)
{
    if (!m_stack)
        return;
    m_view->setSelectionOutline(m_selection.outlines()); // the guide line goes
    // Less than two screen pixels is a click, not a drag.
    if (QLineF(from, to).length() * m_view->zoom() < 2.0)
        return;
    commitFloating();
    easeletch::TileStore *store = editStore();
    if (!store)
        return;
    easeletch::Gradient g;
    g.from = from;
    g.to = to;
    g.shape = m_gradient.shape;
    g.stops = currentGradientStops();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    auto before = easeletch::fillGradient(*store, m_selection, canvasRect(), g);
    QApplication::restoreOverrideCursor();
    if (before.isEmpty())
        return;
    m_history.push(tr("Gradient"), m_stack->activeId(), std::move(before), m_editMask);
    if (m_gradient.preset.startsWith(QLatin1String("colour-")))
        m_color->noteUsed(m_color->color());
    m_view->refresh();
    historyChanged();
}

// --- Filters ------------------------------------------------------------------------

bool MainWindow::previewFilter(const easeletch::Filter &filter)
{
    if (!m_stack || m_view->isStroking())
        return false;
    if (!m_filterPreview.active) {
        commitFloating();
        easeletch::TileStore *store = editStore();
        if (!store)
            return false;
        m_filterPreview = FilterPreview();
        m_filterPreview.active = true;
        m_filterPreview.layerId = m_stack->activeId();
        m_filterPreview.onMask = m_editMask;
        m_filterPreview.original = store->snapshot();
    }
    easeletch::Layer *l = m_stack->layer(m_filterPreview.layerId);
    if (!l) {
        m_filterPreview = FilterPreview();
        return false;
    }
    easeletch::TileStore &store = m_filterPreview.onMask ? l->mask : l->store;
    // Back to the original first, so tries don't pile up.
    for (auto it = m_filterPreview.before.cbegin(); it != m_filterPreview.before.cend(); ++it)
        store.setTile(it.key(), it.value());
    QApplication::setOverrideCursor(Qt::BusyCursor);
    m_filterPreview.before = easeletch::applyFilter(store, m_selection, canvasRect(), filter);
    QApplication::restoreOverrideCursor();
    m_filterPreview.label = FilterDialog::typeName(filter.type);
    m_lastFilters[int(filter.type)] = filter.normalized();
    m_view->refresh();
    return !m_filterPreview.before.isEmpty();
}

void MainWindow::endFilterPreview(bool keep)
{
    if (!m_filterPreview.active)
        return;
    FilterPreview preview = std::move(m_filterPreview);
    m_filterPreview = FilterPreview();
    easeletch::Layer *l = m_stack ? m_stack->layer(preview.layerId) : nullptr;
    if (!l)
        return;
    if (keep && !preview.before.isEmpty()) {
        m_history.push(preview.label, preview.layerId, std::move(preview.before), preview.onMask);
        historyChanged();
    } else {
        easeletch::TileStore &store = preview.onMask ? l->mask : l->store;
        for (auto it = preview.before.cbegin(); it != preview.before.cend(); ++it)
            store.setTile(it.key(), it.value());
    }
    m_view->refresh();
}

bool MainWindow::applyFilter(const easeletch::Filter &filter)
{
    endFilterPreview(false);
    const bool changed = previewFilter(filter);
    const bool began = m_filterPreview.active;
    endFilterPreview(true);
    if (began) {
        m_lastFilterType = int(filter.type);
        m_repeatFilterAct->setEnabled(true);
        m_repeatFilterAct->setText(tr("&Repeat %1").arg(FilterDialog::typeName(filter.type)));
    }
    return changed;
}

void MainWindow::showFilterDialog(easeletch::FilterType type)
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    if (!editStore())
        return; // the status bar says why
    FilterDialog dlg(m_lastFilters[int(type)], this);
    connect(&dlg, &FilterDialog::previewRequested, this, &MainWindow::previewFilter);
    connect(&dlg, &FilterDialog::previewCleared, this, [this] { endFilterPreview(false); });
    previewFilter(dlg.filter());
    if (dlg.exec() == QDialog::Accepted) {
        // Whatever the preview shows, what's applied is the settings as they ended.
        endFilterPreview(false);
        if (!applyFilter(dlg.filter()))
            statusBar()->showMessage(tr("%1 changed nothing here").arg(FilterDialog::typeName(type)), 4000);
    } else {
        endFilterPreview(false);
    }
}

void MainWindow::repeatFilter()
{
    if (m_lastFilterType >= 0)
        applyFilter(m_lastFilters[m_lastFilterType]);
}
