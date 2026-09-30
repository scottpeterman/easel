#include "mainwindow.h"

#include "brushoptionsbar.h"
#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "newdocumentdialog.h"

#include <QAction>
#include <QActionGroup>
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
#include <QToolBar>

#include <memory>

using easel::BrushMode;

namespace {

constexpr int kSettingsVersion = 2;

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

    m_brush = new BrushTool(this);
    m_eyedropper = new EyedropperTool(this);
    m_view->setTool(m_brush);
    m_view->setAltTool(m_eyedropper);

    createDocks();
    createToolBars();
    createActions();
    createStatusBar();

    QSettings settings;
    m_lastDir = settings.value(QStringLiteral("lastDir")).toString();
    m_brush->loadSettings(settings);
    m_color->loadSettings(settings);
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray(), kSettingsVersion);
    if (!settings.contains(QStringLiteral("geometry")))
        resize(1400, 900);

    newDocument(QSize(2000, 1500), Qt::white);
}

MainWindow::~MainWindow()
{
    // The canvas holds pointers to the tools; detach before they go.
    m_view->setTool(nullptr);
    m_view->setAltTool(nullptr);
}

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

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    m_undoAct = edit->addAction(tr("&Undo"), this, &MainWindow::undo);
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct = edit->addAction(tr("&Redo"), this, &MainWindow::redo);
    m_redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    auto *smaller = edit->addAction(tr("Smaller Brush"), this, [this] { m_brush->scaleSize(1.0 / 1.2); });
    smaller->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    auto *larger = edit->addAction(tr("Larger Brush"), this, [this] { m_brush->scaleSize(1.2); });
    larger->setShortcut(QKeySequence(Qt::Key_BracketRight));

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
    for (QToolBar *bar : findChildren<QToolBar *>())
        m_viewMenu->addAction(bar->toggleViewAction());

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    auto *about = help->addAction(tr("&About Easel"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createToolBars()
{
    auto *tools = new QToolBar(tr("Tools"), this);
    tools->setObjectName(QStringLiteral("ToolsBar"));
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBar(Qt::LeftToolBarArea, tools);

    auto *group = new QActionGroup(this);
    m_brushAct = tools->addAction(tr("Brush"), this, [this] { selectBrushMode(int(BrushMode::Paint)); });
    m_brushAct->setShortcut(QKeySequence(Qt::Key_B));
    m_brushAct->setToolTip(tr("Brush (B)"));
    m_eraserAct = tools->addAction(tr("Eraser"), this, [this] { selectBrushMode(int(BrushMode::Erase)); });
    m_eraserAct->setShortcut(QKeySequence(Qt::Key_E));
    m_eraserAct->setToolTip(tr("Eraser (E)"));
    m_eyedropperAct = tools->addAction(tr("Picker"), this, &MainWindow::selectEyedropper);
    m_eyedropperAct->setShortcut(QKeySequence(Qt::Key_I));
    m_eyedropperAct->setToolTip(tr("Eyedropper (I). Hold Alt with any tool to pick once."));
    for (QAction *a : {m_brushAct, m_eraserAct, m_eyedropperAct}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    m_brushAct->setChecked(true);
    connect(m_brush, &BrushTool::modeChanged, this, [this](BrushMode mode) {
        if (m_view->tool() == m_brush)
            (mode == BrushMode::Erase ? m_eraserAct : m_brushAct)->setChecked(true);
    });

    m_options = new BrushOptionsBar(m_brush, this);
    addToolBar(Qt::TopToolBarArea, m_options);

    // The cursor circle follows size changes immediately.
    connect(m_brush, &BrushTool::settingsChanged, m_view, qOverload<>(&QWidget::update));
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
    connect(m_color, &ColorPanel::colorChanged, m_brush, &BrushTool::setColor);
    connect(m_color, &ColorPanel::colorChanged, m_eyedropper, &EyedropperTool::setCurrentColor);
    connect(m_eyedropper, &EyedropperTool::colorPicked, m_color, &ColorPanel::setColor);
    m_brush->setColor(m_color->color());
    m_eyedropper->setCurrentColor(m_color->color());
    // Colours actually painted with go to the Recent row.
    connect(m_brush, &BrushTool::strokeCommitted, this, [this] {
        if (m_brush->mode() == BrushMode::Paint)
            m_color->noteUsed(m_brush->color());
    });

    m_historyList = new QListWidget;
    auto *historyDock = new QDockWidget(tr("History"), this);
    historyDock->setObjectName(QStringLiteral("HistoryDock"));
    historyDock->setWidget(m_historyList);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
    connect(m_historyList, &QListWidget::itemClicked, this, &MainWindow::historyItemClicked);
    connect(m_brush, &BrushTool::strokeCommitted, this, &MainWindow::historyChanged);
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

void MainWindow::selectBrushMode(int mode)
{
    m_brush->setMode(BrushMode(mode));
    m_view->setTool(m_brush);
    m_options->setEnabled(true);
}

void MainWindow::selectEyedropper()
{
    m_view->setTool(m_eyedropper);
    m_options->setEnabled(false); // brush options don't apply
}

void MainWindow::newDocument(const QSize &size, const QColor &background)
{
    setDocument(std::make_unique<easel::TileStore>(background), size, tr("Untitled"),
                tr("New %1 × %2").arg(size.width()).arg(size.height()));
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
    setDocument(std::move(doc.store), doc.size, info.fileName(), tr("Open %1").arg(info.fileName()),
                std::move(doc.pyramid));
    emit documentOpened(path, true);
}

void MainWindow::setDocument(std::unique_ptr<easel::TileStore> layer, const QSize &size,
                             const QString &name, const QString &historyLabel,
                             easel::TilePyramid pyramid)
{
    // Detach everything from the old document before it's freed.
    m_brush->setDocument(nullptr, {}, nullptr);
    m_eyedropper->setDocument(nullptr, {});
    m_history.reset(historyLabel);

    m_layer = std::move(layer);
    m_size = size;
    m_name = name;
    m_view->setDocument(m_layer.get(), m_size, std::move(pyramid));
    m_brush->setDocument(m_layer.get(), QRect(QPoint(0, 0), m_size), &m_history);
    m_eyedropper->setDocument(m_layer.get(), QRect(QPoint(0, 0), m_size));

    m_layers->clear();
    m_layers->addItem(m_name == tr("Untitled") ? tr("Background") : m_name);
    m_layers->setCurrentRow(0);

    historyChanged();
    updateTitle();
    updateMemoryLabel();
}

void MainWindow::undo()
{
    if (!m_layer || m_view->isStroking() || !m_history.canUndo())
        return;
    m_history.undo(*m_layer);
    m_view->refresh();
    historyChanged();
}

void MainWindow::redo()
{
    if (!m_layer || m_view->isStroking() || !m_history.canRedo())
        return;
    m_history.redo(*m_layer);
    m_view->refresh();
    historyChanged();
}

void MainWindow::historyItemClicked(QListWidgetItem *item)
{
    if (!m_layer || m_view->isStroking())
        return;
    m_history.jumpTo(m_historyList->row(item), *m_layer);
    m_view->refresh();
    historyChanged();
}

void MainWindow::historyChanged()
{
    // Row 0 is the base state; row i is "after entry i". Rows past the current
    // position are undone steps you can still click to redo.
    const QSignalBlocker block(m_historyList);
    m_historyList->clear();
    const QString base = m_history.droppedCount() > 0
                             ? tr("… %1 older steps").arg(m_history.droppedCount())
                             : m_history.baseLabel();
    m_historyList->addItem(base);
    for (qsizetype i = 0; i < m_history.count(); ++i)
        m_historyList->addItem(m_history.label(i));

    const QColor undone = palette().color(QPalette::Disabled, QPalette::Text);
    for (int row = int(m_history.position()) + 1; row < m_historyList->count(); ++row)
        m_historyList->item(row)->setForeground(undone);
    m_historyList->setCurrentRow(int(m_history.position()));
    m_historyList->scrollToItem(m_historyList->currentItem());

    m_undoAct->setEnabled(m_history.canUndo());
    m_undoAct->setText(m_history.canUndo() ? tr("&Undo %1").arg(m_history.undoLabel()) : tr("&Undo"));
    m_redoAct->setEnabled(m_history.canRedo());
    m_redoAct->setText(m_history.canRedo() ? tr("&Redo %1").arg(m_history.redoLabel()) : tr("&Redo"));
    updateMemoryLabel();
}

void MainWindow::updateTitle()
{
    setWindowTitle(tr("%1 (%2 × %3) — Easel").arg(m_name).arg(m_size.width()).arg(m_size.height()));
}

void MainWindow::updateMemoryLabel()
{
    const qint64 bytes = m_layer ? m_layer->memoryBytes() : 0;
    const qsizetype tiles = m_layer ? m_layer->tileCount() : 0;
    const QLocale loc;
    const QString text = tr("%1 tiles, %2 · Undo %3 · GPU %4")
                             .arg(tiles)
                             .arg(loc.formattedDataSize(bytes),
                                  loc.formattedDataSize(m_history.memoryBytes()),
                                  loc.formattedDataSize(m_view->gpuMemoryBytes()));
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
    m_color->saveSettings(settings);
    m_brush->saveSettings(settings);
    event->accept();
}
