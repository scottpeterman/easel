#pragma once

#include "tileatlas.h"
#include "tilepyramid.h"
#include "tilestore.h"

#include <QPointF>
#include <QRhiWidget>
#include <QSize>
#include <QTransform>

#include <array>
#include <memory>
#include <vector>

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

// GPU canvas: draws one TileStore with pan, zoom and rotate through QRhi
// (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux).
//
// Tiles live in RGBA16F atlas pages on the GPU. When zoomed out, tiles come
// from a mip pyramid, so the number of quads drawn is bounded by the window
// size, not the canvas size. Uploads are capped per frame; tiles not yet on
// the GPU are drawn from a coarser level until they arrive.
class CanvasView : public QRhiWidget
{
    Q_OBJECT

public:
    static constexpr double MinZoom = 0.01;
    static constexpr double MaxZoom = 64.0;

    struct FrameStats {
        quint64 frame = 0;
        int level = 0;
        int instances = 0;
        int uploads = 0;
        int fallbacks = 0;
        double cpuMs = 0.0;
    };

    explicit CanvasView(QWidget *parent = nullptr);
    ~CanvasView() override;

    void setDocument(easel::TileStore *store, const QSize &canvasSize);
    // With a pyramid already built over store (e.g. on a loader thread).
    void setDocument(easel::TileStore *store, const QSize &canvasSize, easel::TilePyramid pyramid);
    QSize canvasSize() const { return m_canvasSize; }

    double zoom() const { return m_zoom; }
    double rotation() const { return m_rotation; }
    QPointF pan() const { return m_pan; }
    void setPan(const QPointF &pan);

    QTransform canvasToView() const;
    QPointF viewToCanvas(const QPointF &viewPos) const;

    // Pulls dirty tiles from the store and schedules a redraw.
    void refresh();

    void setUploadBudget(int tilesPerFrame) { m_uploadBudget = qMax(1, tilesPerFrame); }
    int uploadBudget() const { return m_uploadBudget; }

    FrameStats lastFrameStats() const { return m_stats; }
    qint64 gpuMemoryBytes() const;

public slots:
    void zoomBy(double factor, const QPointF &anchor);
    void zoomIn();
    void zoomOut();
    void setZoomCentered(double zoom);
    void actualSize();
    void fitToWindow();
    void rotateBy(double degrees);
    void resetRotation();

signals:
    void viewChanged(double zoom, double rotation);
    void cursorMoved(const QPointF &canvasPos, bool insideCanvas);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void createPipelines();
    void ensurePages(int count);
    void resetResidency();
    void updateCursor();
    void emitViewChanged();

    easel::TileStore *m_store = nullptr;
    QSize m_canvasSize;
    double m_zoom = 1.0;
    double m_rotation = 0.0; // degrees, clockwise
    QPointF m_pan;           // canvas centre offset from view centre, view pixels

    easel::TilePyramid m_pyramid;
    easel::TileAtlas m_atlas;
    int m_uploadBudget = 64;
    quint64 m_frame = 0;
    FrameStats m_stats;
    bool m_defaultSlotDirty = true;

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiBuffer> m_quad;
    bool m_quadUploaded = false;
    std::unique_ptr<QRhiBuffer> m_instances;
    quint32 m_instanceCapacity = 0;
    std::unique_ptr<QRhiBuffer> m_outline;
    std::unique_ptr<QRhiSampler> m_nearest;
    std::unique_ptr<QRhiSampler> m_linear;
    std::vector<std::unique_ptr<QRhiTexture>> m_pages;
    std::vector<std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2>> m_pageBindings;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_tilePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlinePipeline;

    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_lastPos;
};
