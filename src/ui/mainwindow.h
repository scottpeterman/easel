#pragma once

#include "history.h"
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
class CanvasView;
class EyedropperTool;
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

public slots:
    void undo();
    void redo();
    bool save();
    bool saveAs();

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
    QMenu *m_viewMenu = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_brushAct = nullptr;
    QAction *m_eraserAct = nullptr;
    QAction *m_eyedropperAct = nullptr;
    QAction *m_fitAct = nullptr;
    QAction *m_actualAct = nullptr;

    QLabel *m_posLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_rotationLabel = nullptr;
    QLabel *m_memoryLabel = nullptr;
    bool m_reportedRenderFailure = false;
    quint64 m_openGeneration = 0;
    bool m_busy = false;
};
