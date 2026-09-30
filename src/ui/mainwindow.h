#pragma once

#include "history.h"
#include "regionops.h"
#include "selection.h"
#include "tilepyramid.h"
#include "tilestore.h"

#include <QMainWindow>
#include <QSize>

#include <memory>

namespace easel {
struct LoadedDocument;
}

class BrushOptionsBar;
class BrushTool;
class CanvasTool;
class CanvasView;
class EyedropperTool;
class MoveTool;
class SelectTool;
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
    // Opens an .easel document or an image in the background; documentOpened()
    // reports the outcome.
    void openDocument(const QString &path);
    // Writes an .easel file. With wait, returns once written (true on success);
    // otherwise saves in the background and documentSaved() reports the outcome.
    bool saveDocumentTo(const QString &path, bool wait = false);

    bool isModified() const;
    QString documentPath() const { return m_path; }

    CanvasView *canvasView() const { return m_view; }
    easel::TileStore *layer() const { return m_layer.get(); }
    BrushTool *brushTool() const { return m_brush; }
    EyedropperTool *eyedropperTool() const { return m_eyedropper; }
    ColorPanel *colorPanel() const { return m_color; }
    const easel::History &history() const { return m_history; }
    SelectTool *rectSelectTool() const { return m_rectSelect; }
    SelectTool *ellipseSelectTool() const { return m_ellipseSelect; }
    MoveTool *moveTool() const { return m_move; }

    const easel::Selection &selection() const { return m_selection; }
    void setSelection(const easel::Selection &selection);
    // Pasted or lifted pixels not yet committed.
    bool isFloating() const { return m_floating.isActive(); }
    QPoint floatingPosition() const { return m_floating.position(); }

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

signals:
    void documentOpened(const QString &path, bool ok);
    void documentSaved(const QString &path, bool ok);
    void imageExported(const QString &path, bool ok);

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
    void storeClip(const QImage &content, const easel::Selection &shape, const QPoint &origin);
    QRect visibleCanvasRect() const;
    QRect canvasRect() const { return QRect(QPoint(0, 0), m_size); }
    void updateSelectionActions();
    void showNewDialog();
    void showOpenDialog();
    void showExportDialog();
    QString askSavePath();
    // Offers to save unsaved changes. False if the user cancelled.
    bool maybeSave();
    void finishSave(const QString &path, quint64 stateId, quint64 docGeneration, const QString &error);
    void showAbout();
    void setDocument(std::unique_ptr<easel::TileStore> layer, const QSize &size,
                     const QString &name, const QString &historyLabel,
                     easel::TilePyramid pyramid = {}, const QString &path = {});
    void finishOpen(const QString &path, quint64 generation, easel::LoadedDocument doc);
    void historyChanged();
    void historyItemClicked(QListWidgetItem *item);
    void updateTitle();
    void updateMemoryLabel();

    std::unique_ptr<easel::TileStore> m_layer;
    QSize m_size;
    QString m_name;
    QString m_lastDir;
    QString m_path;           // the .easel file this document saves to; empty if none
    quint64 m_cleanId = 0;    // history state last saved (or opened)
    quint64 m_docGeneration = 0;
    int m_pendingJobs = 0;    // background saves and exports in flight
    easel::History m_history;

    CanvasView *m_view = nullptr;
    BrushTool *m_brush = nullptr;
    EyedropperTool *m_eyedropper = nullptr;
    BrushOptionsBar *m_options = nullptr;
    QListWidget *m_layers = nullptr;
    QListWidget *m_historyList = nullptr;
    ColorPanel *m_color = nullptr;
    SelectTool *m_rectSelect = nullptr;
    SelectTool *m_ellipseSelect = nullptr;
    MoveTool *m_move = nullptr;

    easel::Selection m_selection;
    easel::FloatingContent m_floating;
    easel::Selection m_selectionBeforeFloat; // restored on cancel
    QString m_floatLabel;                    // history label when committed
    QPoint m_floatStart;                     // where the floating pixels began
    QPointF m_dragStart;
    QPoint m_dragOrigin;
    bool m_moveDragging = false;

    // The last copy, kept at full precision. The system clipboard gets an
    // 8-bit copy tagged with m_clip.token so a paste can tell it's ours.
    struct Clip {
        QImage content;          // RGBA16F, transparent outside the shape
        easel::Selection shape;  // relative to the content's top-left
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
