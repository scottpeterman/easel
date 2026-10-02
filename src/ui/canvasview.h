#pragma once

#include "brush.h"
#include "tileatlas.h"
#include "tilepyramid.h"
#include "tilestore.h"

#include <QPointF>
#include <QPolygonF>
#include <QRhiWidget>
#include <QTimer>
#include <QSize>
#include <QTransform>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class QKeyEvent;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

// Something that acts on the canvas with pen or mouse strokes (brush, eraser).
// Positions are in canvas pixels; pressure is 0..1 (1 for a mouse).
class CanvasTool
{
public:
    virtual ~CanvasTool() = default;
    virtual void press(const easeletch::StrokeSample &s) = 0;
    virtual void move(const easeletch::StrokeSample &s) = 0;
    virtual void release(const easeletch::StrokeSample &s) = 0;
    // Diameter of the cursor circle in canvas pixels; 0 hides it.
    virtual double cursorDiameter() const = 0;
    // A before/after colour ring to draw around the cursor (eyedropper).
    virtual bool colorPreview(QColor *picked, QColor *previous) const
    {
        Q_UNUSED(picked);
        Q_UNUSED(previous);
        return false;
    }
    virtual Qt::CursorShape cursorShape() const { return Qt::CrossCursor; }
    // Keys pressed while the canvas has focus; return true if handled.
    virtual bool keyPress(QKeyEvent *event)
    {
        Q_UNUSED(event);
        return false;
    }
};

// GPU canvas: draws one TileStore (the layers' composite) with pan, zoom and rotate through QRhi
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

    void setDocument(easeletch::TileStore *store, const QSize &canvasSize);
    // With a pyramid already built over store (e.g. on a loader thread).
    void setDocument(easeletch::TileStore *store, const QSize &canvasSize, easeletch::TilePyramid pyramid);
    QSize canvasSize() const { return m_canvasSize; }

    double zoom() const { return m_zoom; }
    double rotation() const { return m_rotation; }
    QPointF pan() const { return m_pan; }
    bool isAutoFit() const { return m_autoFit; }
    void setPan(const QPointF &pan);

    QTransform canvasToView() const;
    QPointF viewToCanvas(const QPointF &viewPos) const;
    // The canvas's bounding box in view coordinates (rotation included).
    QRectF canvasViewBounds() const;

    // Pulls dirty tiles from the store and schedules a redraw.
    void refresh();
    // Called at the start of every refresh(), so whatever feeds the store (the
    // layer compositor) can bring it up to date first.
    void setBeforeRefresh(std::function<void()> hook) { m_beforeRefresh = std::move(hook); }
    // The same store and size, but anything may have changed: redraws
    // everything and keeps the view where it is.
    void reloadDocument();

    // Left-button / pen strokes go to the tool; null means view-only.
    void setTool(CanvasTool *tool);
    CanvasTool *tool() const { return m_tool; }
    // Used instead while Alt is held (the eyedropper, from any tool).
    void setAltTool(CanvasTool *tool) { m_altTool = tool; }

    // Marching ants around this outline (canvas coordinates); empty for none.
    void setSelectionOutline(const QList<QPolygonF> &outline);
    // Grab handles (a transform's): four-cornered shapes in canvas
    // coordinates, drawn solid white with a dark edge over everything else.
    void setHandles(const QList<QPolygonF> &handles);
    QList<QPolygonF> handles() const { return m_handles; }
    // A line between every pixel, from 600% zoom up.
    void setPixelGridVisible(bool visible);
    bool pixelGridVisible() const { return m_pixelGrid; }
    // A grid of sprite cells: cell size, and where the first cell starts.
    void setCellGrid(bool visible, const QSize &cell, const QPoint &offset);
    bool cellGridVisible() const { return m_cellGrid; }
    // The next zoom in (direction > 0) or out: whole-pixel steps above 100%.
    double steppedZoom(double zoom, int direction) const;
    QList<QPolygonF> selectionOutline() const { return m_selectionOutline; }
    bool isStroking() const { return m_stroking; }

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
    void strokeFinished();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void createPipelines();
    void ensurePages(int count);
    void resetResidency();
    void updateCursor();
    void emitViewChanged();
    bool cursorVisible() const;
    easeletch::StrokeSample sampleAt(const QPointF &viewPos, double pressure) const;
    void beginStroke(const easeletch::StrokeSample &s);
    void continueStroke(const easeletch::StrokeSample &s);
    void endStroke(const easeletch::StrokeSample &s);
    void trackCursor(const QPointF &viewPos);
    void startPan(const QPointF &viewPos);
    CanvasTool *activeTool() const;
    void setAltHeld(bool held);

    easeletch::TileStore *m_store = nullptr;
    std::function<void()> m_beforeRefresh;
    easeletch::Pixel m_shownDefault; // the store's default pixel as last drawn
    QSize m_canvasSize;
    double m_zoom = 1.0;
    bool m_autoFit = true; // refit on resize until the user navigates
    double m_rotation = 0.0; // degrees, clockwise
    QPointF m_pan;           // canvas centre offset from view centre, view pixels

    easeletch::TilePyramid m_pyramid;
    easeletch::TileAtlas m_atlas;
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
    std::unique_ptr<QRhiGraphicsPipeline> m_fillPipeline; // same shaders, triangles
    std::unique_ptr<QRhiGraphicsPipeline> m_linesPipeline; // same shaders, separate segments
    std::unique_ptr<QRhiBuffer> m_ants;
    quint32 m_antsCapacity = 0;

    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_lastPos;

    CanvasTool *m_tool = nullptr;
    CanvasTool *m_altTool = nullptr;
    CanvasTool *m_strokeTool = nullptr; // the tool that owns the current stroke
    bool m_altHeld = false;
    bool m_stroking = false;
    easeletch::StrokeSample m_lastSample;
    bool m_hovering = false;
    QPointF m_cursorCanvas;

    QList<QPolygonF> m_selectionOutline;
    QList<QPolygonF> m_handles;
    bool m_pixelGrid = true;
    bool m_cellGrid = false;
    QSize m_cellSize{32, 32};
    QPoint m_cellOffset;
    double m_wheelNotches = 0.0;
    QTimer *m_antsTimer = nullptr;
    int m_antsPhase = 0;
};
