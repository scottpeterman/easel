#pragma once

#include "tilestore.h"

#include <QMainWindow>
#include <QSize>

#include <memory>

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
    bool openImage(const QString &path);

    CanvasView *canvasView() const { return m_view; }

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
                     const QString &name);
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
};
