#pragma once

#include "tilepyramid.h"
#include "tilestore.h"

#include <QMainWindow>
#include <QSize>

#include <memory>

namespace easel {
struct LoadedDocument;
}

class CanvasView;
class ColorPanel;
class QLabel;
class QListWidget;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void newDocument(const QSize &size, const QColor &background);
    // Loads in the background; documentOpened() reports the outcome.
    void openImage(const QString &path);

    CanvasView *canvasView() const { return m_view; }

signals:
    void documentOpened(const QString &path, bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createDocks();
    void createStatusBar();
    void showNewDialog();
    void showOpenDialog();
    void showAbout();
    void setDocument(std::unique_ptr<easel::TileStore> layer, const QSize &size,
                     const QString &name, easel::TilePyramid pyramid = {});
    void finishOpen(const QString &path, quint64 generation, easel::LoadedDocument doc);
    void logHistory(const QString &entry);
    void updateTitle();
    void updateMemoryLabel();

    std::unique_ptr<easel::TileStore> m_layer;
    QSize m_size;
    QString m_name;
    QString m_lastDir;

    CanvasView *m_view = nullptr;
    QListWidget *m_layers = nullptr;
    QListWidget *m_history = nullptr;
    ColorPanel *m_color = nullptr;
    QMenu *m_viewMenu = nullptr;

    QLabel *m_posLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_rotationLabel = nullptr;
    QLabel *m_memoryLabel = nullptr;
    bool m_reportedRenderFailure = false;
    quint64 m_openGeneration = 0;
    bool m_busy = false;
};
