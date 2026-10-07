#pragma once

#include "documentio.h"
#include "fillops.h"
#include "filters.h"
#include "griddialog.h"
#include "history.h"
#include "layerstack.h"
#include "regionops.h"
#include "selection.h"
#include "shade.h"
#include "tilepyramid.h"
#include "tilestore.h"
#include "transform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QSize>

#include <memory>
#include <utility>
#include <vector>


class BrushOptionsBar;
class BrushTool;
class CloneSourceTool;
class CanvasTool;
class CanvasView;
class EyedropperTool;
class LayerPanel;
class AdjustPanel;
class MoveTool;
class SelectTool;
class WandTool;
class LassoTool;
class TextTool;
class ShapeTool;
class QSettings;
class TextPanel;
class TransformTool;
class FillTool;
class GradientTool;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QToolButton;
class QDoubleSpinBox;
class QToolBar;
class ColorPanel;
class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QTabBar;

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

    // Pages. A document holds one or more drawings, each with its own canvas
    // size, layers, undo history, selection and view; they're saved together
    // in the one .easeletch file. Everything else in this window works on the
    // current page.
    int pageCount() const { return int(m_pages.size()); }
    int currentPage() const { return m_page; }
    QString pageName(int index) const;
    QSize pageSize(int index) const;
    QTabBar *pageTabs() const { return m_pageTabs; }

    CanvasView *canvasView() const { return m_view; }
    // The active layer's pixels (null while a group is active).
    easeletch::TileStore *layer() const;
    const easeletch::LayerStack &layers() const { return *m_stack; }
    LayerPanel *layerPanel() const { return m_layerPanel; }
    AdjustPanel *adjustPanel() const { return m_adjustPanel; }
    BrushTool *brushTool() const { return m_brush; }
    // Switches the Brush tool to a ready-made brush (see brushpresets.h) and
    // makes it the tool in use. False if there's no such brush.
    bool chooseBrushPreset(const QString &id);
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
    // Opens a text layer's words in the text panel again, to change them or
    // how they're drawn (what a click on placed text with the Text tool
    // does). False if the layer isn't text any more, or can't be changed.
    bool editText(int layerId);
    // The topmost text layer with its words under a canvas point; 0 if none.
    int textLayerAt(const QPoint &pos) const;
    // Places the text on its layer (one undo step) / drops it, or the
    // changes to it.
    void commitText();
    void cancelText();
    // Shapes: polygons and lines drawn point by point, each on a layer of
    // its own that keeps its points, so they can be moved afterwards.
    ShapeTool *shapeTool() const { return m_shapeTool; }
    // How the Shape tool draws. The fill is the painting colour.
    struct ShapeOptions {
        int line = 3; // outline width; 0 = none
        QColor lineColor = Qt::black;
        bool filled = false;
        // The fill is the Gradient tool's current gradient, not the one colour.
        bool gradient = false;
        bool curved = false; // a curve through the points
        bool closed = true;  // the last point joins the first
        bool smooth = true;  // soft edges; off for hard pixels
        // An open line thinning to a point at its start / its end: how much
        // of its length each takes, in percent.
        int taperStart = 0;
        int taperEnd = 0;
        // A point put down or moved near a point of another shape lands on it.
        bool snap = true;
    };
    const ShapeOptions &shapeOptions() const { return m_shapeOpts; }
    // Changes them, and the shape being drawn or changed with them.
    void setShapeOptions(const ShapeOptions &options);
    // A shape is being drawn or changed and hasn't been put down yet.
    bool isShaping() const { return m_shape.active; }
    QList<QPointF> shapePoints() const;
    // The shape being drawn or changed, as it stands.
    easeletch::ShapeSettings shapeInProgress() const { return m_shape.shape; }
    // Starts a shape at its first point / adds the next (what clicks with
    // the Shape tool do).
    void beginShape(const QPointF &first);
    void addShapePoint(const QPointF &pos);
    // Opens a shape layer's points again, to move, add or remove them (what
    // a click on a placed shape with the Shape tool does). False if the
    // layer isn't a shape any more, or can't be changed.
    bool editShape(int layerId);
    // The topmost shape layer under a canvas point; 0 if none.
    int shapeLayerAt(const QPointF &pos) const;
    // Puts the shape down on its layer (one undo step) / drops it, or the
    // changes to it.
    void commitShape();
    void cancelShape();
    TransformTool *transformTool() const { return m_transformTool; }
    FillTool *fillTool() const { return m_fillTool; }
    GradientTool *gradientTool() const { return m_gradientTool; }
    // How the Fill tool finds the area to fill, as the magic wand does.
    // allLayers: it looks at the whole picture, not just the active layer
    // (line art on one layer, colour on another).
    struct FillOptions {
        double tolerance = 0.12;
        bool contiguous = true;
        bool allLayers = false;
    };
    const FillOptions &fillOptions() const { return m_fill; }
    void setFillOptions(const FillOptions &options);
    // Which gradient the Gradient tool draws, and how it spreads.
    // preset: "colour-transparent" (the painting colour fading out),
    // "colour-end" (the painting colour to the end colour), "colour-shaded"
    // (the painting colour with a highlight and a shadow), "custom" (the stops
    // last edited), the id of a built-in (easeletch::gradientPresets()), or of
    // one saved by the user ("user:" + its name).
    struct GradientOptions {
        easeletch::GradientShape shape = easeletch::GradientShape::Linear;
        QString preset = QStringLiteral("colour-transparent");
        QColor end = Qt::white;
        bool reverse = false;
        easeletch::GradientStops custom;
    };
    const GradientOptions &gradientOptions() const { return m_gradient; }
    void setGradientOptions(const GradientOptions &options);
    // The stops a drag would draw now: the preset, with the painting colour
    // where it uses it, reversed if asked.
    easeletch::GradientStops currentGradientStops() const;
    // Gradients saved from the editor. Saving under a name already there replaces it.
    const QList<easeletch::GradientPreset> &userGradients() const { return m_userGradients; }
    QString saveUserGradient(const QString &name, const easeletch::GradientStops &stops);
    bool removeUserGradient(const QString &id);
    // Opens the stop editor on the current gradient.
    void editGradient();
    // A free transform is under way: the pixels float with a box round them.
    bool isTransforming() const { return m_xf.active; }
    const easeletch::FreeTransform &transformBox() const { return m_xf.box; }
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

    // Floods the area of similar colour around a canvas point with the
    // painting colour, inside the selection if there is one (what a click
    // with the Fill tool does). One undo step.
    void fillAt(const QPoint &pos);
    // Fills the selection, or the whole layer, with the painting colour.
    void fillSelection();
    // Draws a gradient between two canvas points over the selection, or the
    // whole layer (what a drag with the Gradient tool does). One undo step.
    void drawGradient(const QPointF &from, const QPointF &to);

    // Runs a filter over the active layer, inside the selection if there is
    // one. One undo step. False if nothing changed or the layer can't be edited.
    bool applyFilter(const easeletch::Filter &filter);
    // The same, tried out: the canvas shows the result, and nothing is
    // recorded until endFilterPreview(true). Each call replaces the last.
    bool previewFilter(const easeletch::Filter &filter);
    void endFilterPreview(bool keep);
    bool isPreviewingFilter() const { return m_filterPreview.active; }
    // Opens a filter's settings, previewing on the canvas as they change.
    void showFilterDialog(easeletch::FilterType type);
    // Runs the last filter used again, with the same settings.
    void repeatFilter();

    // Shade Areas: a gradient on every enclosed area of the active layer (or
    // those mostly inside the selection), on a Multiply layer just above it:
    // the one already there, or a new one. One undo step. False if there was
    // nothing to shade or the layer can't be read.
    bool shadeAreas(const easeletch::ShadeSettings &settings, const easeletch::AreaOptions &options = {});
    // The same, tried out: the canvas shows it, and nothing is recorded until
    // endShadePreview(true). Each call replaces the last.
    bool previewShade(const easeletch::ShadeSettings &settings, const easeletch::AreaOptions &options);
    void endShadePreview(bool keep);
    bool isPreviewingShade() const { return m_shade.active; }
    void showShadeDialog();

    // Free transform of the selected pixels, or of everything on the layer
    // when nothing is selected. Enter (commitFloating) applies it as one undo
    // step, Escape (cancelFloating) drops it. False if there's nothing to
    // transform.
    bool beginTransform();
    // Sets the scale (1 = unchanged, negative flips) and rotation (degrees,
    // clockwise) of the transform under way; the centre stays where it is.
    void setTransform(double scaleX, double scaleY, double angle);
    // Smooth resampling, or hard pixels (nearest neighbour) for sprites.
    void setTransformSmooth(bool smooth);
    bool transformSmooth() const { return m_transformSmooth; }
    // Four-corner warp: each corner of the transform under way is dragged on
    // its own, and what's between follows in perspective (a texture laid on
    // a wing, a sign on a wall seen at an angle). Turning it on starts from
    // the box as it stands; turning it off goes back to that box.
    void setTransformWarp(bool on);
    bool transformWarp() const { return m_xf.active && m_xf.warp; }
    // The four corners on the canvas: top-left, top-right, bottom-right,
    // bottom-left of what's being transformed.
    QPolygonF transformCorners() const;
    // Puts one corner somewhere (what dragging its handle does). False if
    // that would fold the shape over or dent it; nothing changes then.
    bool setTransformCorner(int corner, const QPointF &pos);
    // Each acts on the transform under way, or is one undo step of its own.
    void flipHorizontal();
    void flipVertical();
    // Quarter turns clockwise; negative for counter-clockwise.
    void rotateQuarter(int turns);

    // Layers. Each is one undo step and returns false if it couldn't be done.
    // New layers go above the active one and become active.
    bool addLayer();
    // Adds an adjustment layer above the active one: it changes the look of
    // everything below it and stays editable.
    bool addAdjustmentLayer(easeletch::AdjustmentType type);
    // Changes an adjustment layer's settings. A run of changes to the same
    // layer (dragging a slider or a curve point) is one undo step.
    void setAdjustment(int id, const easeletch::Adjustment &adjustment);
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

    // Pages. Adding, removing, renaming and reordering pages aren't undo
    // steps (each page has its own history); they do count as unsaved changes.
    // Each returns false if it couldn't be done.
    // A new page goes after the current one and becomes current.
    bool addPage(const QSize &size, const QColor &background);
    // A copy of the current page, layers and all.
    bool duplicatePage();
    // The last page can't be deleted.
    bool deletePage(int index);
    bool renamePage(int index, const QString &name);
    bool movePage(int from, int to);
    bool setCurrentPage(int index);

signals:
    void documentOpened(const QString &path, bool ok);
    void documentSaved(const QString &path, bool ok);
    void imageExported(const QString &path, bool ok);
    void wandSettingsLoaded();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void createActions();
    void createToolBars();
    // Shares the right-hand column out between the docks, the first time
    // the window is shown with no saved layout.
    void balanceDocks();
    // The Brush button's list of ready-made brushes, and Undo / Redo at the
    // foot of the tool strip (for a tablet, where there's no Ctrl+Z).
    void createBrushPresets();
    void createStripUndo();
    void syncBrushPreset();
    void createDocks();
    void createStatusBar();
    void createTransformOptions();
    void createFillOptions();
    void createGradientOptions();
    // Shows the brush's mirror lines on the canvas while they apply.
    void updateSymmetryGuides();
    void createShapeOptions();
    void syncShapeOptions();
    // Redraws the shape being worked on from its points and the options.
    void updateShape();
    void showShapeHandles();
    // Where a point being put down or moved goes: on a whole pixel; with
    // Shift held, in line with the point before it at a 15° step; otherwise
    // on a point of another shape, if one's close by. index: which point of
    // the shape it is (the number of points, for one about to be added).
    QPointF shapePointFor(const QPointF &pos, int index) const;
    // Where the curve's handles for the point last touched are, to draw and
    // to grab, each with its side (1 the way out of the point, 2 the way
    // in): none unless a curved shape is being changed.
    QList<std::pair<QPointF, int>> shapeCurveHandles() const;
    // Copies the Gradient tool's list of gradients into the Shape options.
    void mirrorGradientChoices();
    void shapePressed(const QPointF &pos);
    void shapeDragged(const QPointF &pos);
    void shapeReleased(const QPointF &pos);
    void shapeKey(int key);
    void loadShapeSettings(QSettings &s);
    void saveShapeSettings(QSettings &s) const;
    void syncFillOptions();
    void syncGradientOptions();
    void rebuildGradientPresets();
    easeletch::GradientStops presetStops(const QString &id) const;
    QAction *actionFor(CanvasTool *tool) const;
    // interactive: switches to the Transform tool and shows the box.
    bool startTransform(bool interactive);
    // Redraws the floating pixels from the box. final: at full quality (a
    // drag on a large block previews with hard pixels).
    void applyTransform(bool final);
    void updateTransformOutline();
    void syncTransformOptions();
    // Forgets the transform session and goes back to the tool used before it.
    void endTransform();
    void quickTransform(const QString &label, void (easeletch::FreeTransform::*change)());
    void transformPressed(const QPointF &pos);
    void transformDragged(const QPointF &pos);
    void transformReleased(const QPointF &pos);
    void selectBrushMode(int mode);
    void selectEyedropper();
    // Clone tool: Alt picks its source rather than a colour, and a marker
    // shows where the source is.
    void updateCloneTool();
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
    // Brings the Adjustment panel forward for an adjustment layer, the Color
    // panel for any other.
    void showPanelForActiveLayer();
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
    // Shows the text panel, the first time in a corner away from pos.
    void showTextPanel(const QPoint &pos);
    // A diamond on the tip of the frame's tail, to drag it by; nothing when
    // there's no tail.
    void showTailHandle();
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
    // What a save wrote, so that exactly that can be marked as saved when it
    // finishes: painting goes on while a save is written.
    struct SavedState {
        quint64 docGeneration = 0;
        quint64 pagesRevision = 0;
        QList<QPair<int, quint64>> pages; // page id, its history state
    };
    void finishSave(const QString &path, const SavedState &state, const QString &error);
    void showAbout();
    // pages: every page of an opened document in order, the active one
    // (stack) with a null entry at activePage; empty for a one-page document.
    void setDocument(std::unique_ptr<easeletch::LayerStack> stack, const QString &name, const QString &historyLabel,
                     easeletch::TilePyramid pyramid = {}, const QString &path = {},
                     std::vector<easeletch::LoadedPage> pages = {}, int activePage = 0);

    struct Page;
    // Takes the current page out of the window's hands (view, tools, panels)
    // and keeps it in m_pages / brings a kept page back as the current one.
    void parkPage();
    void unparkPage(int index);
    // Makes a page of a stack, after the current one, and switches to it.
    bool insertPage(std::unique_ptr<easeletch::LayerStack> stack, const QString &name, const QString &historyLabel);
    // False while the page can't be left: a stroke is under way. Places
    // anything floating first.
    bool leavePage();
    QString uniquePageName(const QString &base) const;
    // After pages were added, removed, renamed or reordered.
    void pagesChanged();
    void syncPageTabs();
    void createPageBar();
    void createPageMenu();
    void showNewPageDialog();
    void showRenamePageDialog(int index);
    void confirmDeletePage(int index);
    void showPageMenu(const QPoint &pos);
    void finishOpen(const QString &path, quint64 generation, easeletch::LoadedDocument doc);
    void historyChanged();
    void historyItemClicked(QListWidgetItem *item);
    void updateTitle();
    void updateMemoryLabel();

    // The current page's layers, history (m_history), saved state (m_cleanId)
    // and selection (m_selection) are members of the window, as for a
    // document of one drawing. The other pages wait in m_pages, which also
    // has an entry for the current page: its name and id, with the rest
    // empty until it's parked.
    struct Page {
        int id = 0; // stays with the page when it's moved or others are deleted
        QString name;
        std::unique_ptr<easeletch::LayerStack> stack;
        easeletch::History history;
        quint64 cleanId = 0;
        easeletch::Selection selection;
        // Where the view was. fitted: it was following the window's size.
        bool fitted = true;
        double zoom = 1.0;
        double rotation = 0.0;
        QPointF pan;
    };
    std::vector<Page> m_pages;
    int m_page = 0;
    int m_nextPageId = 1;
    // Counts changes to the page list itself; differs from m_cleanRevision
    // when one hasn't been saved.
    quint64 m_pagesRevision = 0;
    quint64 m_cleanRevision = 0;
    QTabBar *m_pageTabs = nullptr;
    bool m_syncingTabs = false;
    QAction *m_deletePageAct = nullptr;
    QAction *m_nextPageAct = nullptr;
    QAction *m_prevPageAct = nullptr;

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
    AdjustPanel *m_adjustPanel = nullptr;
    QDockWidget *m_adjustDock = nullptr;
    QDockWidget *m_colorDock = nullptr;
    bool m_layoutRestored = false;
    bool m_docksBalanced = false;
    // The adjustment layer and history state the last settings entry belongs to.
    int m_adjustLayer = 0;
    quint64 m_adjustState = 0;
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
        bool editing = false; // changing text already placed, not typing new
        QString nameBefore;   // the name the layer would have been given for its old words
        easeletch::LayerStack before;
        QPoint anchor;  // the point the text hangs from (its corner, or centre / right edge)
        QPoint placed;  // where the floating image was last put
        QRect box;      // the block of text, where it was last put
        QPoint centre;  // the middle of the words, where they were last put
        bool hasTail = false;
        QPoint tailTip; // where the frame's tail pointed when last drawn
        bool draggingTail = false;
        bool dragging = false;
        bool clickStarts = false; // the press under way will start new text when it ends
        bool movingLayer = false; // ... or it became a drag, and is moving the layer
        easeletch::Selection selectionBeforeMove;
    } m_text;
    // A filter being tried out. The layer's tiles as they were are kept, so
    // each new setting starts from the original, never from the last try.
    struct FilterPreview {
        bool active = false;
        int layerId = 0;
        bool onMask = false;
        easeletch::TileStore original;
        QHash<easeletch::TileCoord, QImage> before; // what the try now showing replaced
        QString label;
    } m_filterPreview;
    // Shade Areas being tried out: the layers as they were, the areas found
    // (finding them is the slow part, so it's done once), and what the try
    // now showing replaced.
    struct ShadePreview {
        bool active = false;
        easeletch::LayerStack before;
        int targetId = 0;
        bool found = false;
        easeletch::AreaOptions options;
        easeletch::AreaShader shader;
        QHash<easeletch::TileCoord, QImage> painted;
    } m_shade;
    easeletch::ShadeSettings m_lastShade;
    easeletch::AreaOptions m_lastShadeOptions;
    int m_lastShadeGradient = 0;
    easeletch::Filter m_lastFilters[easeletch::FilterTypeCount];
    int m_lastFilterType = -1;
    QAction *m_repeatFilterAct = nullptr;
    FillTool *m_fillTool = nullptr;
    GradientTool *m_gradientTool = nullptr;
    QAction *m_fillAct = nullptr;
    QAction *m_gradientAct = nullptr;
    QToolBar *m_fillOptions = nullptr;
    QToolBar *m_gradientOptions = nullptr;
    QToolBar *m_shapeOptions = nullptr;
    QSpinBox *m_shapeLine = nullptr;
    QToolButton *m_shapeLineColor = nullptr;
    QCheckBox *m_shapeFilled = nullptr;
    QCheckBox *m_shapeGradient = nullptr;
    QComboBox *m_shapeGradientPreset = nullptr;
    QComboBox *m_shapeGradientShape = nullptr;
    QCheckBox *m_shapeCurved = nullptr;
    QCheckBox *m_shapeClosed = nullptr;
    QCheckBox *m_shapeSmooth = nullptr;
    QSpinBox *m_shapeTaperStart = nullptr;
    QSpinBox *m_shapeTaperEnd = nullptr;
    QCheckBox *m_shapeSnap = nullptr;
    ShapeTool *m_shapeTool = nullptr;
    QAction *m_shapeAct = nullptr;
    ShapeOptions m_shapeOpts;
    // A shape being drawn or changed. Like text being typed, it lives on its
    // layer floating until it's put down; "before" is the document without it
    // (or with it as it was).
    struct ShapeSession {
        bool active = false;
        bool adding = false;  // clicks add points (a new shape); otherwise they move them
        bool editing = false; // changing a shape already placed
        int layerId = 0;
        easeletch::LayerStack before;
        easeletch::ShapeSettings shape;
        int selected = -1;    // the point last touched
        int dragPoint = -1;   // the point the drag under way is moving
        int dragGradient = 0; // ... or an end of the gradient's line: 1 its start, 2 its end
        int dragHandle = 0;   // ... or a handle of the selected point: 1 the way out, 2 the way in
        // A shape opened again keeps the gradient it has, until another is
        // picked for it; a new one takes the Gradient tool's.
        bool ownGradient = false;
        bool dragWhole = false;
        QList<QPointF> dragStart; // the points when a drag of the whole shape began
        QPointF pressAt;
        bool pressNew = false; // Ctrl was held: a new shape, even on top of one already placed
        // What the press under way does once it's over.
        enum After { Nothing, Start, Finish } afterRelease = Nothing;
        QElapsedTimer lastClick; // for telling a double-click
        QPointF lastClickAt;
    } m_shape;
    FillOptions m_fill;
    GradientOptions m_gradient;
    QSpinBox *m_fillTolerance = nullptr;
    QCheckBox *m_fillContiguous = nullptr;
    QCheckBox *m_fillAllLayers = nullptr;
    QComboBox *m_gradientShape = nullptr;
    QComboBox *m_gradientPreset = nullptr;
    QToolButton *m_gradientRemove = nullptr;
    QList<easeletch::GradientPreset> m_userGradients;
    QCheckBox *m_gradientReverse = nullptr;
    QToolButton *m_gradientEnd = nullptr;
    TransformTool *m_transformTool = nullptr;
    QAction *m_transformAct = nullptr;
    QToolBar *m_transformOptions = nullptr;
    QDoubleSpinBox *m_xfWidth = nullptr;
    QDoubleSpinBox *m_xfHeight = nullptr;
    QDoubleSpinBox *m_xfAngle = nullptr;
    QCheckBox *m_xfSmooth = nullptr;
    bool m_transformSmooth = true;
    qint64 m_lastSmoothMs = 0; // how long the last smooth redraw took
    // A free transform under way. The pixels float (m_floating); source is
    // what they were when it began, and every change redraws them from it, so
    // scaling down and back up loses nothing.
    struct TransformSession {
        bool active = false;
        QImage source;
        easeletch::Selection shape; // relative to the source's top-left
        easeletch::FreeTransform box;
        easeletch::FreeTransform start; // when the drag under way began
        easeletch::TransformHandle handle = easeletch::TransformHandle::None;
        QPointF dragFrom;
        bool dragging = false;
        bool hadSelection = false; // otherwise it's the whole layer, and nothing stays selected
        bool changed = false;
        CanvasTool *previousTool = nullptr;
        // Four-corner warp: the corners are where they've been put, not
        // where the box would have them.
        bool warp = false;
        QPolygonF quad;
        QPolygonF quadStart; // when the drag under way began
        int corner = -1;     // the corner the drag is moving; -1: all of them
    } m_xf;
    QCheckBox *m_xfWarp = nullptr;
    QList<QWidget *> m_xfBoxOnly; // the options that only mean something without the warp
    bool m_editMask = false;
    bool m_floatMask = false; // the floating pixels are on a mask
    QToolBar *m_wandOptions = nullptr;
    double m_colorToAlphaThreshold = 0.04;

    easeletch::Selection m_selection;
    easeletch::FloatingContent m_floating;
    easeletch::Selection m_selectionBeforeFloat; // restored on cancel
    QString m_floatLabel;                    // history label when committed
    bool m_floatPasted = false;              // the floating pixels came from Paste
    int m_floatLayer = 0;                    // the layer the pixels float over
    // Dragging the opacity slider is one undo step: the layer and history
    // state the last opacity entry belongs to.
    int m_opacityLayer = 0;
    quint64 m_opacityState = 0;
    mutable bool m_opacityRedrawPending = false;
    // A move of a whole text layer: the text goes with it (see commitFloating).
    bool m_floatText = false;
    easeletch::LayerStack m_floatTextBefore;
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
    QToolBar *m_toolsBar = nullptr;
    QMenu *m_brushMenu = nullptr;
    QListWidget *m_brushList = nullptr;
    bool m_brushWasActive = false; // when its button was pressed
    QAction *m_eraserAct = nullptr;
    QAction *m_eyedropperAct = nullptr;
    QAction *m_smudgeAct = nullptr;
    QAction *m_cloneAct = nullptr;
    QAction *m_healAct = nullptr;
    CloneSourceTool *m_cloneSource = nullptr; // Alt+click while the Clone tool is active
    bool m_cloneMarker = false;               // the source marker is on the canvas
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
