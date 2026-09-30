#include "mainwindow.h"

#include "brushoptionsbar.h"
#include "brushtool.h"
#include "canvasarea.h"
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
#include <QImageWriter>
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
#include <QToolButton>

#include <memory>

using easel::BrushMode;

namespace {

constexpr int kSettingsVersion = 2;

QString openFilter()
{
    QStringList images;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        images << QStringLiteral("*.") + QString::fromLatin1(fmt);
    const QString native = QStringLiteral("*.") + QLatin1String(easel::NativeSuffix);
    return QObject::tr("All supported (%1 %2);;Easel documents (%1);;Images (%2);;All files (*)")
        .arg(native, images.join(QLatin1Char(' ')));
}

QString withSuffix(const QString &path, const QString &suffix)
{
    return QFileInfo(path).suffix().isEmpty() ? path + QLatin1Char('.') + suffix : path;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_view = new CanvasView;
    setCentralWidget(new CanvasArea(m_view, this));

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

    auto *openAct = file->addAction(tr("&Open..."), this, &MainWindow::showOpenDialog);
    openAct->setShortcut(QKeySequence::Open);

    file->addSeparator();
    auto *saveAct = file->addAction(tr("&Save"), this, &MainWindow::save);
    saveAct->setShortcut(QKeySequence::Save);
    auto *saveAsAct = file->addAction(tr("Save &As..."), this, &MainWindow::saveAs);
    saveAsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    auto *exportAct = file->addAction(tr("&Export..."), this, &MainWindow::showExportDialog);
    exportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

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

    m_fitAct = m_viewMenu->addAction(tr("&Fit to Window"), m_view, &CanvasView::fitToWindow);
    m_fitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_fitAct->setIconText(tr("Fit"));
    m_fitAct->setToolTip(tr("Fit the canvas to the window (Ctrl+0)"));

    m_actualAct = m_viewMenu->addAction(tr("&Actual Pixels"), m_view, &CanvasView::actualSize);
    m_actualAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_actualAct->setIconText(tr("1:1"));
    m_actualAct->setToolTip(tr("Actual pixels, 100% (Ctrl+1)"));

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
    for (QAction *a : {m_fitAct, m_actualAct}) {
        auto *b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        statusBar()->addPermanentWidget(b);
    }
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

void MainWindow::openDocument(const QString &path)
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
        auto doc = std::make_shared<easel::LoadedDocument>(easel::loadDocument(path));
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
        QMessageBox::warning(this, tr("Open"),
                             tr("Could not open %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), doc.error));
        emit documentOpened(path, false);
        return;
    }

    const QFileInfo info(path);
    m_lastDir = info.absolutePath();
    // Only an .easel file is saved back to; an opened image gets Save As.
    setDocument(std::move(doc.store), doc.size, info.fileName(), tr("Open %1").arg(info.fileName()),
                std::move(doc.pyramid), doc.native ? info.absoluteFilePath() : QString());
    emit documentOpened(path, true);
}

bool MainWindow::isModified() const
{
    return m_history.stateId() != m_cleanId;
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    return saveDocumentTo(m_path);
}

bool MainWindow::saveAs()
{
    const QString path = askSavePath();
    return !path.isEmpty() && saveDocumentTo(path);
}

QString MainWindow::askSavePath()
{
    const QString suffix = QLatin1String(easel::NativeSuffix);
    QString suggested = m_path;
    if (suggested.isEmpty())
        suggested = QDir(m_lastDir).filePath(QFileInfo(m_name).completeBaseName() + QLatin1Char('.') + suffix);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested,
                                                      tr("Easel documents (*.%1)").arg(suffix));
    return path.isEmpty() ? QString() : withSuffix(path, suffix);
}

bool MainWindow::saveDocumentTo(const QString &path, bool wait)
{
    if (!m_layer)
        return false;
    // The snapshot shares tiles with the live document (copy-on-write), so
    // painting can go on while it's written out.
    easel::TileStore snapshot = m_layer->snapshot();
    const QSize size = m_size;
    const quint64 stateId = m_history.stateId();
    const quint64 docGeneration = m_docGeneration;
    m_lastDir = QFileInfo(path).absolutePath();

    if (wait) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        const QString error = easel::saveNativeDocument(path, std::move(snapshot), size);
        QApplication::restoreOverrideCursor();
        ++m_pendingJobs; // balanced in finishSave
        finishSave(path, stateId, docGeneration, error);
        return error.isEmpty();
    }

    statusBar()->showMessage(tr("Saving %1…").arg(QFileInfo(path).fileName()));
    ++m_pendingJobs;
    QPointer<MainWindow> self(this);
    auto shared = std::make_shared<easel::TileStore>(std::move(snapshot));
    QThreadPool::globalInstance()->start([self, path, shared, size, stateId, docGeneration] {
        const QString error = easel::saveNativeDocument(path, std::move(*shared), size);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, stateId, docGeneration, error] {
                if (self)
                    self->finishSave(path, stateId, docGeneration, error);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void MainWindow::finishSave(const QString &path, quint64 stateId, quint64 docGeneration, const QString &error)
{
    --m_pendingJobs;
    const QString name = QFileInfo(path).fileName();
    if (!error.isEmpty()) {
        statusBar()->clearMessage();
        QMessageBox::warning(this, tr("Save"),
                             tr("Could not save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        emit documentSaved(path, false);
        return;
    }
    // A different document may be open by now; only mark this one clean.
    if (docGeneration == m_docGeneration) {
        m_path = QFileInfo(path).absoluteFilePath();
        m_name = name;
        m_cleanId = stateId; // later strokes still count as unsaved
        updateTitle();
    }
    statusBar()->showMessage(tr("Saved %1").arg(name), 4000);
    emit documentSaved(path, true);
}

bool MainWindow::maybeSave()
{
    if (!isModified())
        return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("Save changes to %1 before closing it?").arg(m_name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    const QString path = m_path.isEmpty() ? askSavePath() : m_path;
    return !path.isEmpty() && saveDocumentTo(path, true);
}

void MainWindow::setDocument(std::unique_ptr<easel::TileStore> layer, const QSize &size,
                             const QString &name, const QString &historyLabel,
                             easel::TilePyramid pyramid, const QString &path)
{
    // Detach everything from the old document before it's freed.
    m_brush->setDocument(nullptr, {}, nullptr);
    m_eyedropper->setDocument(nullptr, {});
    m_history.reset(historyLabel);
    m_cleanId = m_history.stateId();
    ++m_docGeneration;
    m_path = path;

    m_layer = std::move(layer);
    m_size = size;
    m_name = name;
    m_view->setDocument(m_layer.get(), m_size, std::move(pyramid));
    m_brush->setDocument(m_layer.get(), QRect(QPoint(0, 0), m_size), &m_history);
    m_eyedropper->setDocument(m_layer.get(), QRect(QPoint(0, 0), m_size));

    m_layers->clear();
    m_layers->addItem(tr("Background"));
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
    setWindowModified(isModified());
    updateMemoryLabel();
}

void MainWindow::updateTitle()
{
    // [*] shows as "*" when the document has unsaved changes.
    setWindowTitle(tr("%1[*] (%2 × %3) — Easel").arg(m_name).arg(m_size.width()).arg(m_size.height()));
    setWindowModified(isModified());
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
    if (!maybeSave())
        return;
    NewDocumentDialog dlg(m_size.isEmpty() ? QSize(2000, 1500) : m_size, this);
    if (dlg.exec() == QDialog::Accepted)
        newDocument(dlg.canvasSize(), dlg.background());
}

void MainWindow::showOpenDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), m_lastDir, openFilter());
    if (!path.isEmpty())
        openDocument(path);
}

void MainWindow::showExportDialog()
{
    if (!m_layer)
        return;
    QStringList filters{tr("PNG image (*.png)"), tr("JPEG image (*.jpg *.jpeg)")};
    if (QImageWriter::supportedImageFormats().contains("webp"))
        filters << tr("WebP image (*.webp)");
    const QString suggested = QDir(m_lastDir).filePath(QFileInfo(m_name).completeBaseName() + QStringLiteral(".png"));
    QString selected;
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), suggested, filters.join(QStringLiteral(";;")),
                                                &selected);
    if (path.isEmpty())
        return;
    const QString suffix = selected.contains(QLatin1String("jpg")) ? QStringLiteral("jpg")
                           : selected.contains(QLatin1String("webp")) ? QStringLiteral("webp")
                                                                     : QStringLiteral("png");
    path = withSuffix(path, suffix);
    m_lastDir = QFileInfo(path).absolutePath();

    statusBar()->showMessage(tr("Exporting %1…").arg(QFileInfo(path).fileName()));
    ++m_pendingJobs;
    QPointer<MainWindow> self(this);
    auto snapshot = std::make_shared<easel::TileStore>(m_layer->snapshot());
    const QSize size = m_size;
    QThreadPool::globalInstance()->start([self, path, snapshot, size] {
        const QString error = easel::exportImage(path, *snapshot, size);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, error] {
                if (!self)
                    return;
                --self->m_pendingJobs;
                if (error.isEmpty()) {
                    self->statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 4000);
                } else {
                    self->statusBar()->clearMessage();
                    QMessageBox::warning(self, tr("Export"),
                                         tr("Could not export %1:\n%2").arg(QDir::toNativeSeparators(path), error));
                }
                emit self->imageExported(path, error.isEmpty());
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Easel"),
                       tr("<b>Easel</b> %1<br>A balanced layered image editor.<br>Qt %2")
                           .arg(QStringLiteral(EASEL_VERSION), QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    if (m_pendingJobs > 0) {
        // Let background saves and exports finish writing their files.
        QApplication::setOverrideCursor(Qt::BusyCursor);
        QThreadPool::globalInstance()->waitForDone();
        QApplication::restoreOverrideCursor();
    }

    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState(kSettingsVersion));
    settings.setValue(QStringLiteral("lastDir"), m_lastDir);
    m_color->saveSettings(settings);
    m_brush->saveSettings(settings);
    event->accept();
}
