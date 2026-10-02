#pragma once

#include "griddialog.h"
#include "history.h"
#include "layerstack.h"
#include "regionops.h"
#include "selection.h"
#include "tilepyramid.h"
#include "tilestore.h"

#include <QMainWindow>
#include <QSize>

#include <memory>

namespace easeletch {
struct LoadedDocument;
}

class BrushOptionsBar;
class BrushTool;
class CanvasTool;
class CanvasView;
class EyedropperTool;
class LayerPanel;
class MoveTool;
class SelectTool;
class WandTool;
class LassoTool;
class TextTool;
class TextPanel;
class QToolBar;
class ColorPanel;
class QAction;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void newDocument(const QSize &size, const QColor &background);
    // Opens an .easeletch document or an image in the background; documentOpened()
    // reports the outcome.
    void openDocument(const QString &path);
    // Writes an .easeletch file. With wait, returns once written (true on success);
    // otherwise saves in the background and documentSaved() reports the outcome.
    bool saveDocumentTo(const QString &path, bool wait = false);

    bool isModified() const;
    QString documentPath() const { return m_path; }

    CanvasView *canvasView() const { return m_view; }
    // The active layer's pixels (null while a group is active).
    easeletch::TileStore *layer() const;
    const easeletch::LayerStack &layers() const { return *m_stack; }
    LayerPanel *layerPanel() const { return m_layerPanel; }
    BrushTool *brushTool() const { return m_brush; }
    EyedropperTool *eyedropperTool() const { return m_eyedropper; }
    ColorPanel *colorPanel() const { return m_color; }
    const easeletch::History &history() const { return m_history; }
    SelectTool *rectSelectTool() const { return m_rectSelect; }
    SelectTool *ellipseSelectTool() const { return m_ellipseSelect; }
    MoveTool *moveTool() const { return m_move; }
    WandTool *wandTool() const { return m_wand; }
    LassoTool *lassoTool() const { return m_lasso; }
    TextTool *textTool() const { return m_textTool; }
    TextPanel *textPanel() const { return m_textPanel; }
    // Text is being typed and hasn't been placed yet.
    bool isTyping() const { return m_text.active; }
    // Starts a block of text with its corner at a canvas point (what a click
    // with the Text tool does). The words come from the text panel.
    void beginText(const QPoint &pos);
    // Places the text on its new layer (one undo step) / drops it.
    void commitText();
    void cancelText();
    // Painting and editing go to the active layer's mask, not its pixels.
    bool isEditingMask() const { return m_editMask; }

    const easeletch::Selection &selection() const { return m_selection; }
    void setSelection(const easeletch::Selection &selection);
    // Pasted or lifted pixels not yet committed.
    bool isFloating() const { return m_floating.isActive(); }
    QPoint floatingPosition() const { return m_floating.position(); }
    QSize canvasSize() const { return m_stack ? m_stack->size() : QSize(); }

    const GridSettings &gridSettings() const { return m_grid; }
    void setGridSettings(const GridSettings &grid);
    // Writes the selected pixels (transparent outside the shape) as an image.
    bool exportSelectionTo(const QString &path);

public slots:
    void undo();
    void redo();
    bool save();
    bool saveAs();

    void cut();
    void copy();
    void paste();
    void deleteSelection();
    void selectAll();
    void deselect();
    // Drops floating pixels where they are (one undo step) / puts them back.
    void commitFloating();
    void cancelFloating();
    // Crops the canvas to the selection's bounds (undoable).
    void cropToSelection();
    // Crops away fully transparent edges.
    void trim();
    void invertSelection();
    // Grows (pixels > 0) or shrinks (< 0) the selection.
    void growSelection(int pixels);
    // Fades the selection's edge over about that many pixels.
    void featherSelection(int pixels);
    // Color to Alpha in the selection, or everywhere without one.
    void colorToAlpha(const QColor &color, double threshold);

    // Layers. Each is one undo step and returns false if it couldn't be done.
    // New layers go above the active one and become active.
    bool addLayer();
    // Puts the active layer into a new group.
    bool addGroup();
    bool duplicateLayer();
    bool deleteLayer();
    // Merges the active layer into the one below, or a group into one layer.
    bool mergeDown();
    bool flattenImage();
    bool raiseLayer();
    bool lowerLayer();
    void setActiveLayer(int id);
    void setLayerVisible(int id, bool visible);
    void setLayerLocked(int id, bool locked);
    void renameLayer(int id, const QString &name);
    void setLayerOpacity(int id, double opacity);
    void setLayerBlend(int id, easeletch::BlendMode mode);
    // The whole stack in a new order, bottom to top: (layer id, parent id).
    bool rearrangeLayers(const QList<QPair<int, int>> &order);
    // Masks. A new mask shows everything, or only the selection if there is
    // one, and becomes what the brush paints on.
    bool addLayerMask();
    bool deleteLayerMask();
    bool applyLayerMask();
    void setLayerMaskEnabled(int id, bool enabled);
    void setEditingMask(bool on);

signals:
    void documentOpened(const QString &path, bool ok);
    void documentSaved(const QString &path, bool ok);
    void imageExported(const QString &path, bool ok);
    void wandSettingsLoaded();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createToolBars();
    void createDocks();
    void createStatusBar();
    void selectBrushMode(int mode);
    void selectEyedropper();
    // Switches the canvas tool, committing floating pixels unless it's Move.
    void activateTool(CanvasTool *tool, bool brushOptions);
    // Floats the selection (or the whole canvas) so Move can place it.
    bool liftForMove();
    void moveDragStarted(const QPointF &pos);
    void moveDragged(const QPointF &pos);
    void nudge(const QPoint &delta);
    // Clears the selected pixels, or discards floating ones, as one undo step.
    void clearSelected(const QString &label);
    void storeClip(const QImage &content, const easeletch::Selection &shape, const QPoint &origin);
    QRect visibleCanvasRect() const;
    QRect canvasRect() const { return QRect(QPoint(0, 0), canvasSize()); }
    // The active layer's pixels if they can be changed now; otherwise null,
    // with the reason in the status bar.
    easeletch::TileStore *editStore();
    // What copy, the wand and export read: the active layer, or the whole
    // picture while a group is active.
    const easeletch::TileStore *readStore() const;
    // Points the tools at the active layer.
    void bindTools();
    // Before a change to the stack: false if it can't happen now.
    bool beginLayerChange();
    void finishLayerChange(const QString &label, easeletch::LayerStack before);
    // After the stack changed: recomposite, rebind, refresh the panel.
    void layersChanged();
    bool moveLayerBy(int step);
    void flushOpacityRedraw();
    // Brings the composite up to date with the layers.
    void syncComposite() const;
    void updateSelectionActions();
    void showNewDialog();
    void showOpenDialog();
    void showExportDialog();
    void showExportSelectionDialog();
    void showGridDialog();
    void showColorToAlphaDialog();
    void wandClicked(const QPointF &pos, Qt::KeyboardModifiers modifiers);
    void lassoFinished(const QPolygonF &path, Qt::KeyboardModifiers modifiers);
    // Redraws the text being typed from the panel's current settings.
    void updateText();
    void textPressed(const QPointF &pos);
    void textDragged(const QPointF &pos);
    void textReleased(const QPointF &pos);
    void hideTextPanel();
    // Marching ants around floating pixels where they are now.
    void showFloatingOutline();
    void cropCanvasTo(const QRect &rect, const QString &label);
    // After undo / redo. stackChanged: layers or the canvas size may differ.
    void afterHistoryMove(const QSize &sizeBefore, bool stackChanged);
    // Points the view and tools at the document again after a size change.
    void applyCanvasSize();
    QString askSavePath();
    // Offers to save unsaved changes. False if the user cancelled.
    bool maybeSave();
    void finishSave(const QString &path, quint64 stateId, quint64 docGeneration, const QString &error);
    void showAbout();
    void setDocument(std::unique_ptr<easeletch::LayerStack> stack, const QString &name, const QString &historyLabel,
                     easeletch::TilePyramid pyramid = {}, const QString &path = {});
    void finishOpen(const QString &path, quint64 generation, easeletch::LoadedDocument doc);
    void historyChanged();
    void historyItemClicked(QListWidgetItem *item);
    void updateTitle();
    void updateMemoryLabel();

    std::unique_ptr<easeletch::LayerStack> m_stack;
    QString m_name;
    QString m_lastDir;
    QString m_path;           // the .easeletch file this document saves to; empty if none
    quint64 m_cleanId = 0;    // history state last saved (or opened)
    quint64 m_docGeneration = 0;
    int m_pendingJobs = 0;    // background saves and exports in flight
    easeletch::History m_history;

    CanvasView *m_view = nullptr;
    BrushTool *m_brush = nullptr;
    EyedropperTool *m_eyedropper = nullptr;
    BrushOptionsBar *m_options = nullptr;
    LayerPanel *m_layerPanel = nullptr;
    QListWidget *m_historyList = nullptr;
    ColorPanel *m_color = nullptr;
    SelectTool *m_rectSelect = nullptr;
    SelectTool *m_ellipseSelect = nullptr;
    MoveTool *m_move = nullptr;
    WandTool *m_wand = nullptr;
    LassoTool *m_lasso = nullptr;
    TextTool *m_textTool = nullptr;
    TextPanel *m_textPanel = nullptr;
    QAction *m_textAct = nullptr;
    QPoint m_textPanelPos; // where the text panel was last left; null until it's first shown
    // Text being typed. It lives on a layer made for it, floating like pasted
    // pixels, until it's placed; "before" is the document without that layer.
    struct TextSession {
        bool active = false;
        int layerId = 0;
        easeletch::LayerStack before;
        QPoint anchor;  // the point the text hangs from (its corner, or centre / right edge)
        QPoint placed;  // where the floating image was last put
        bool dragging = false;
        bool clickStarts = false; // the press under way will start new text when it ends
    } m_text;
    bool m_editMask = false;
    bool m_floatMask = false; // the floating pixels are on a mask
    QToolBar *m_wandOptions = nullptr;
    double m_colorToAlphaThreshold = 0.04;

    easeletch::Selection m_selection;
    easeletch::FloatingContent m_floating;
    easeletch::Selection m_selectionBeforeFloat; // restored on cancel
    QString m_floatLabel;                    // history label when committed
    int m_floatLayer = 0;                    // the layer the pixels float over
    // Dragging the opacity slider is one undo step: the layer and history
    // state the last opacity entry belongs to.
    int m_opacityLayer = 0;
    quint64 m_opacityState = 0;
    mutable bool m_opacityRedrawPending = false;
    QPoint m_floatStart;                     // where the floating pixels began
    QPointF m_dragStart;
    QPoint m_dragOrigin;
    bool m_moveDragging = false;

    // The last copy, kept at full precision. The system clipboard gets an
    // 8-bit copy tagged with m_clip.token so a paste can tell it's ours.
    struct Clip {
        QImage content;          // RGBA16F, transparent outside the shape
        easeletch::Selection shape;  // relative to the content's top-left
        QPoint origin;           // where it was copied from
        QByteArray token;
    } m_clip;
    QMenu *m_viewMenu = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_brushAct = nullptr;
    QAction *m_eraserAct = nullptr;
    QAction *m_eyedropperAct = nullptr;
    QAction *m_smudgeAct = nullptr;
    QAction *m_rectSelectAct = nullptr;
    QAction *m_ellipseSelectAct = nullptr;
    QAction *m_moveAct = nullptr;
    QAction *m_cutAct = nullptr;
    QAction *m_copyAct = nullptr;
    QAction *m_deleteAct = nullptr;
    QAction *m_deselectAct = nullptr;
    QAction *m_cropAct = nullptr;
    QAction *m_wandAct = nullptr;
    QAction *m_lassoAct = nullptr;
    QAction *m_featherAct = nullptr;
    QAction *m_editMaskAct = nullptr;
    int m_featherPixels = 4;
    QAction *m_invertAct = nullptr;
    QAction *m_growAct = nullptr;
    QAction *m_shrinkAct = nullptr;
    int m_growPixels = 2;
    QAction *m_exportSelectionAct = nullptr;
    QAction *m_pixelGridAct = nullptr;
    QAction *m_cellGridAct = nullptr;
    GridSettings m_grid;
    QAction *m_fitAct = nullptr;
    QAction *m_actualAct = nullptr;

    QLabel *m_posLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_rotationLabel = nullptr;
    QLabel *m_memoryLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    bool m_reportedRenderFailure = false;
    quint64 m_openGeneration = 0;
    bool m_busy = false;
};
