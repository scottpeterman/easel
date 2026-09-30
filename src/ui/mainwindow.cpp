#include "mainwindow.h"

#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "newdocumentdialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QThreadPool>

#include <memory>

namespace {

constexpr int kSettingsVersion = 1;

QString imageFilter()
{
    QStringList patterns;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(fmt);
    return QObject::tr("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_view = new CanvasView(this);
    setCentralWidget(m_view);

    createDocks();
    createActions();
    createStatusBar();

    QSettings settings;
    m_lastDir = settings.value(QStringLiteral("lastDir")).toString();
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray(), kSettingsVersion);
    if (!settings.contains(QStringLiteral("geometry")))
        resize(1280, 800);

    newDocument(QSize(2000, 1500), Qt::white);
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));

    auto *newAct = file->addAction(tr("&New..."), this, &MainWindow::showNewDialog);
    newAct->setShortcut(QKeySequence::New);

    auto *openAct = file->addAction(tr("&Open Image..."), this, &MainWindow::showOpenDialog);
    openAct->setShortcut(QKeySequence::Open);

    file->addSeparator();
    auto *quitAct = file->addAction(tr("&Quit"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence::Quit);
    quitAct->setMenuRole(QAction::QuitRole);

    m_viewMenu = menuBar()->addMenu(tr("&View"));

    auto *zoomIn = m_viewMenu->addAction(tr("Zoom &In"), m_view, &CanvasView::zoomIn);
    zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});

    auto *zoomOut = m_viewMenu->addAction(tr("Zoom &Out"), m_view, &CanvasView::zoomOut);
    zoomOut->setShortcut(QKeySequence::ZoomOut);

    auto *fit = m_viewMenu->addAction(tr("&Fit to Window"), m_view, &CanvasView::fitToWindow);
    fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    auto *actual = m_viewMenu->addAction(tr("&Actual Pixels"), m_view, &CanvasView::actualSize);
    actual->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));

    m_viewMenu->addSeparator();

    auto *rotL = m_viewMenu->addAction(tr("Rotate &Left"), this, [this] { m_view->rotateBy(-15.0); });
    rotL->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));

    auto *rotR = m_viewMenu->addAction(tr("Rotate &Right"), this, [this] { m_view->rotateBy(15.0); });
    rotR->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight));

    auto *rotReset = m_viewMenu->addAction(tr("R&eset Rotation"), m_view, &CanvasView::resetRotation);
    rotReset->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));

    m_viewMenu->addSeparator();
    for (QDockWidget *dock : findChildren<QDockWidget *>())
        m_viewMenu->addAction(dock->toggleViewAction());

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    auto *about = help->addAction(tr("&About Easel"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createDocks()
{
    setDockOptions(AnimatedDocks | AllowTabbedDocks | AllowNestedDocks);

    m_layers = new QListWidget;
    auto *layersDock = new QDockWidget(tr("Layers"), this);
    layersDock->setObjectName(QStringLiteral("LayersDock"));
    layersDock->setWidget(m_layers);
    addDockWidget(Qt::RightDockWidgetArea, layersDock);

    m_color = new ColorPanel;
    auto *colorDock = new QDockWidget(tr("Color"), this);
    colorDock->setObjectName(QStringLiteral("ColorDock"));
    colorDock->setWidget(m_color);
    addDockWidget(Qt::RightDockWidgetArea, colorDock);

    m_history = new QListWidget;
    auto *historyDock = new QDockWidget(tr("History"), this);
    historyDock->setObjectName(QStringLiteral("HistoryDock"));
    historyDock->setWidget(m_history);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
}

void MainWindow::createStatusBar()
{
    m_posLabel = new QLabel(this);
    m_zoomLabel = new QLabel(this);
    m_rotationLabel = new QLabel(this);
    m_memoryLabel = new QLabel(this);

    statusBar()->addWidget(m_posLabel, 1);
    statusBar()->addPermanentWidget(m_memoryLabel);
    statusBar()->addPermanentWidget(m_rotationLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    connect(m_view, &CanvasView::viewChanged, this, [this](double zoom, double rotation) {
        m_zoomLabel->setText(tr("%1%").arg(zoom * 100.0, 0, 'f', zoom < 0.1 ? 1 : 0));
        m_rotationLabel->setText(tr("%1°").arg(rotation, 0, 'f', 0));
    });
    connect(m_view, &CanvasView::cursorMoved, this, [this](const QPointF &pos, bool inside) {
        m_posLabel->setText(inside ? tr("%1, %2").arg(int(pos.x())).arg(int(pos.y())) : QString());
    });
    connect(m_view, &CanvasView::frameSubmitted, this, &MainWindow::updateMemoryLabel);
    connect(m_view, &CanvasView::renderFailed, this, [this] {
        if (m_reportedRenderFailure)
            return;
        m_reportedRenderFailure = true;
        QMessageBox::critical(this, tr("Graphics"),
                              tr("The canvas could not start the GPU renderer. "
                                 "Update your graphics driver and try again."));
    });
}

void MainWindow::newDocument(const QSize &size, const QColor &background)
{
    setDocument(std::make_unique<easel::TileStore>(background), size, tr("Untitled"));
    logHistory(tr("New %1 × %2").arg(size.width()).arg(size.height()));
}

void MainWindow::openImage(const QString &path)
{
    // Decoding, tile import and the zoom pyramid all run on a worker thread;
    // the window stays responsive. Opening again supersedes an earlier load.
    const quint64 generation = ++m_openGeneration;
    statusBar()->showMessage(tr("Opening %1…").arg(QFileInfo(path).fileName()));
    if (!m_busy) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        m_busy = true;
    }

    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self, path, generation] {
        auto doc = std::make_shared<easel::LoadedDocument>(easel::loadImageDocument(path));
        QMetaObject::invokeMethod(
            qApp,
            [self, path, generation, doc] {
                if (self)
                    self->finishOpen(path, generation, std::move(*doc));
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::finishOpen(const QString &path, quint64 generation, easel::LoadedDocument doc)
{
    if (generation != m_openGeneration)
        return; // superseded by a later open
    if (m_busy) {
        QApplication::restoreOverrideCursor();
        m_busy = false;
    }
    statusBar()->clearMessage();

    if (!doc.ok()) {
        QMessageBox::warning(this, tr("Open Image"),
                             tr("Could not open %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), doc.error));
        emit documentOpened(path, false);
        return;
    }

    const QFileInfo info(path);
    m_lastDir = info.absolutePath();
    setDocument(std::move(doc.store), doc.size, info.fileName(), std::move(doc.pyramid));
    logHistory(tr("Open %1").arg(info.fileName()));
    emit documentOpened(path, true);
}

void MainWindow::setDocument(std::unique_ptr<easel::TileStore> layer, const QSize &size,
                             const QString &name, easel::TilePyramid pyramid)
{
    m_layer = std::move(layer);
    m_size = size;
    m_name = name;
    m_view->setDocument(m_layer.get(), m_size, std::move(pyramid));

    m_layers->clear();
    m_layers->addItem(m_name == tr("Untitled") ? tr("Background") : m_name);
    m_layers->setCurrentRow(0);
    m_history->clear();

    updateTitle();
    updateMemoryLabel();
}

void MainWindow::logHistory(const QString &entry)
{
    m_history->addItem(entry);
    m_history->setCurrentRow(m_history->count() - 1);
}

void MainWindow::updateTitle()
{
    setWindowTitle(tr("%1 (%2 × %3) — Easel").arg(m_name).arg(m_size.width()).arg(m_size.height()));
}

void MainWindow::updateMemoryLabel()
{
    const qint64 bytes = m_layer ? m_layer->memoryBytes() : 0;
    const qsizetype tiles = m_layer ? m_layer->tileCount() : 0;
    const QString text = tr("%1 tiles, %2 · GPU %3")
                             .arg(tiles)
                             .arg(QLocale().formattedDataSize(bytes),
                                  QLocale().formattedDataSize(m_view->gpuMemoryBytes()));
    if (m_memoryLabel->text() != text)
        m_memoryLabel->setText(text);
}

void MainWindow::showNewDialog()
{
    NewDocumentDialog dlg(m_size.isEmpty() ? QSize(2000, 1500) : m_size, this);
    if (dlg.exec() == QDialog::Accepted)
        newDocument(dlg.canvasSize(), dlg.background());
}

void MainWindow::showOpenDialog()
{
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Image"), m_lastDir, imageFilter());
    if (!path.isEmpty())
        openImage(path);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Easel"),
                       tr("<b>Easel</b> %1<br>A balanced layered image editor.<br>Qt %2")
                           .arg(QStringLiteral(EASEL_VERSION), QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState(kSettingsVersion));
    settings.setValue(QStringLiteral("lastDir"), m_lastDir);
    event->accept();
}
