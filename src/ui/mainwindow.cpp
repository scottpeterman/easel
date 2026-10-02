#include "mainwindow.h"

#include "brushoptionsbar.h"
#include "brushtool.h"
#include "canvasarea.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "colortoalphadialog.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "layerpanel.h"
#include "newdocumentdialog.h"
#include "selecttools.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QImageWriter>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>

#include <climits>
#include <cmath>
#include <memory>

using easeletch::BrushMode;

namespace {

constexpr int kSettingsVersion = 2;
constexpr char kClipMime[] = "application/x-easeletch-clip";

QString openFilter()
{
    QStringList images;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        images << QStringLiteral("*.") + QString::fromLatin1(fmt);
    const QString native = QStringLiteral("*.%1 *.%2").arg(QLatin1String(easeletch::NativeSuffix),
                                                          QLatin1String(easeletch::LegacySuffix));
    return QObject::tr("All supported (%1 %2);;Easeletch documents (%1);;Images (%2);;All files (*)")
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
    m_rectSelect = new SelectTool(easeletch::Selection::Shape::Rect, this);
    m_ellipseSelect = new SelectTool(easeletch::Selection::Shape::Ellipse, this);
    m_move = new MoveTool(this);
    m_wand = new WandTool(this);
    connect(m_wand, &WandTool::clicked, this, &MainWindow::wandClicked);
    m_brush->setSelection(&m_selection);
    connect(m_brush, &BrushTool::blocked, this, [this] { editStore(); });
    // Painting changes a layer; the canvas draws the composite of them all.
    m_view->setBeforeRefresh([this] {
        if (m_stack)
            syncComposite();
    });
    for (SelectTool *t : {m_rectSelect, m_ellipseSelect}) {
        connect(t, &SelectTool::selectionDragged, this, &MainWindow::setSelection);
        connect(t, &SelectTool::selectionFinished, this, &MainWindow::setSelection);
    }
    connect(m_move, &MoveTool::dragStarted, this, &MainWindow::moveDragStarted);
    connect(m_move, &MoveTool::dragged, this, &MainWindow::moveDragged);
    connect(m_move, &MoveTool::dragEnded, this, [this](const QPointF &pos) {
        moveDragged(pos);
        m_moveDragging = false;
    });
    connect(m_move, &MoveTool::nudged, this, &MainWindow::nudge);
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
    GridSettings grid;
    grid.load(settings);
    setGridSettings(grid);
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray(), kSettingsVersion);
    m_wandOptions->hide(); // shown with the wand
    m_options->show();
    m_wand->setTolerance(settings.value(QStringLiteral("wand/tolerance"), m_wand->tolerance()).toDouble());
    m_wand->setContiguous(settings.value(QStringLiteral("wand/contiguous"), m_wand->contiguous()).toBool());
    emit wandSettingsLoaded();
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
    m_exportSelectionAct = file->addAction(tr("Export Se&lection..."), this, &MainWindow::showExportSelectionDialog);
    m_exportSelectionAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E));

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
    m_cutAct = edit->addAction(tr("Cu&t"), this, &MainWindow::cut);
    m_cutAct->setShortcut(QKeySequence::Cut);
    m_copyAct = edit->addAction(tr("&Copy"), this, &MainWindow::copy);
    m_copyAct->setShortcut(QKeySequence::Copy);
    auto *pasteAct = edit->addAction(tr("&Paste"), this, &MainWindow::paste);
    pasteAct->setShortcut(QKeySequence::Paste);
    m_deleteAct = edit->addAction(tr("&Delete"), this, &MainWindow::deleteSelection);
    m_deleteAct->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    edit->addSeparator();
    auto *selectAllAct = edit->addAction(tr("Select &All"), this, &MainWindow::selectAll);
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    m_deselectAct = edit->addAction(tr("D&eselect"), this, &MainWindow::deselect);
    m_deselectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    edit->addSeparator();
    m_invertAct = edit->addAction(tr("&Invert Selection"), this, &MainWindow::invertSelection);
    m_invertAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    m_growAct = edit->addAction(tr("&Grow Selection..."), this, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Grow Selection"), tr("Grow by (pixels):"), m_growPixels, 1, 100,
                                           1, &ok);
        if (ok) {
            m_growPixels = n;
            growSelection(n);
        }
    });
    m_shrinkAct = edit->addAction(tr("S&hrink Selection..."), this, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Shrink Selection"), tr("Shrink by (pixels):"), m_growPixels, 1,
                                           100, 1, &ok);
        if (ok) {
            m_growPixels = n;
            growSelection(-n);
        }
    });
    edit->addSeparator();

    QMenu *image = menuBar()->addMenu(tr("&Image"));
    m_cropAct = image->addAction(tr("C&rop to Selection"), this, &MainWindow::cropToSelection);
    m_cropAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    auto *trimAct = image->addAction(tr("&Trim Transparent Edges"), this, &MainWindow::trim);
    trimAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    image->addSeparator();
    auto *c2a = image->addAction(tr("&Color to Alpha..."), this, &MainWindow::showColorToAlphaDialog);
    c2a->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));

    QMenu *layer = menuBar()->addMenu(tr("&Layer"));
    auto *newLayer = layer->addAction(tr("&New Layer"), this, &MainWindow::addLayer);
    newLayer->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    auto *dupLayer = layer->addAction(tr("&Duplicate Layer"), this, &MainWindow::duplicateLayer);
    dupLayer->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    auto *groupLayer = layer->addAction(tr("&Group Layer"), this, &MainWindow::addGroup);
    groupLayer->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    layer->addAction(tr("D&elete Layer"), this, &MainWindow::deleteLayer);
    layer->addSeparator();
    layer->addAction(tr("Move &Up"), this, &MainWindow::raiseLayer);
    layer->addAction(tr("Move D&own"), this, &MainWindow::lowerLayer);
    layer->addSeparator();
    auto *mergeAct = layer->addAction(tr("&Merge Down"), this, &MainWindow::mergeDown);
    mergeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    layer->addAction(tr("&Flatten Image"), this, &MainWindow::flattenImage);

    auto *smaller = edit->addAction(tr("Smaller Brush"), this, [this] { m_brush->scaleSize(1.0 / 1.2); });
    smaller->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    auto *larger = edit->addAction(tr("Larger Brush"), this, [this] { m_brush->scaleSize(1.2); });
    larger->setShortcut(QKeySequence(Qt::Key_BracketRight));

    // Enter drops floating pixels, Escape puts them back (or deselects). Only
    // while the canvas has focus, so they don't steal keys from dialogs and fields.
    auto *commitAct = new QAction(tr("Commit"), m_view);
    commitAct->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    commitAct->setShortcutContext(Qt::WidgetShortcut);
    connect(commitAct, &QAction::triggered, this, &MainWindow::commitFloating);
    m_view->addAction(commitAct);
    auto *escapeAct = new QAction(tr("Cancel"), m_view);
    escapeAct->setShortcut(QKeySequence(Qt::Key_Escape));
    escapeAct->setShortcutContext(Qt::WidgetShortcut);
    connect(escapeAct, &QAction::triggered, this, [this] {
        if (m_floating.isActive())
            cancelFloating();
        else
            deselect();
    });
    m_view->addAction(escapeAct);
    updateSelectionActions();

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

    m_pixelGridAct = m_viewMenu->addAction(tr("&Pixel Grid"));
    m_pixelGridAct->setCheckable(true);
    m_pixelGridAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    m_pixelGridAct->setToolTip(tr("Lines between pixels from 600% zoom (Ctrl+')"));
    connect(m_pixelGridAct, &QAction::toggled, this, [this](bool on) {
        GridSettings g = m_grid;
        g.pixel = on;
        setGridSettings(g);
    });
    m_cellGridAct = m_viewMenu->addAction(tr("&Sprite Grid"));
    m_cellGridAct->setCheckable(true);
    m_cellGridAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Apostrophe));
    connect(m_cellGridAct, &QAction::toggled, this, [this](bool on) {
        GridSettings g = m_grid;
        g.cells = on;
        setGridSettings(g);
    });
    m_viewMenu->addAction(tr("Sprite &Grid Settings..."), this, &MainWindow::showGridDialog);

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
        if (bar != m_wandOptions && bar != m_options) // these follow the tool
            m_viewMenu->addAction(bar->toggleViewAction());

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    auto *about = help->addAction(tr("&About Easeletch"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    // Easeletch links Qt under the LGPL, which asks for Qt's own notice to be shown.
    auto *aboutQt = help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);
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
    m_smudgeAct = tools->addAction(tr("Smudge"), this, [this] { selectBrushMode(int(BrushMode::Smudge)); });
    m_smudgeAct->setShortcut(QKeySequence(Qt::Key_S));
    m_smudgeAct->setToolTip(tr("Smudge (S): drags and blends colour"));
    m_eyedropperAct = tools->addAction(tr("Picker"), this, &MainWindow::selectEyedropper);
    m_eyedropperAct->setShortcut(QKeySequence(Qt::Key_I));
    m_eyedropperAct->setToolTip(tr("Eyedropper (I). Hold Alt with any tool to pick once."));
    tools->addSeparator();
    m_rectSelectAct = tools->addAction(tr("Rect"), this, [this] { activateTool(m_rectSelect, false); });
    m_rectSelectAct->setShortcut(QKeySequence(Qt::Key_M));
    m_rectSelectAct->setToolTip(tr("Rectangle select (M). Shift for a square; click to deselect."));
    m_ellipseSelectAct = tools->addAction(tr("Ellipse"), this, [this] { activateTool(m_ellipseSelect, false); });
    m_ellipseSelectAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    m_ellipseSelectAct->setToolTip(tr("Ellipse select (Shift+M). Shift for a circle."));
    m_wandAct = tools->addAction(tr("Wand"), this, [this] { activateTool(m_wand, false); });
    m_wandAct->setShortcut(QKeySequence(Qt::Key_W));
    m_wandAct->setToolTip(tr("Magic wand (W): select by colour. Shift adds, Ctrl subtracts."));
    m_moveAct = tools->addAction(tr("Move"), this, [this] { activateTool(m_move, false); });
    m_moveAct->setShortcut(QKeySequence(Qt::Key_V));
    m_moveAct->setToolTip(tr("Move selected pixels (V). Arrows nudge 1 px, Shift+arrows 10. "
                             "Enter drops, Escape cancels."));
    for (QAction *a : {m_brushAct, m_eraserAct, m_smudgeAct, m_eyedropperAct, m_rectSelectAct,
                       m_ellipseSelectAct, m_wandAct, m_moveAct}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    m_brushAct->setChecked(true);
    connect(m_brush, &BrushTool::modeChanged, this, [this](BrushMode mode) {
        if (m_view->tool() == m_brush)
            (mode == BrushMode::Erase    ? m_eraserAct
             : mode == BrushMode::Smudge ? m_smudgeAct
                                         : m_brushAct)
                ->setChecked(true);
    });

    m_options = new BrushOptionsBar(m_brush, this);
    addToolBar(Qt::TopToolBarArea, m_options);

    // Wand options take the brush options' place while the wand is active.
    m_wandOptions = new QToolBar(tr("Wand Options"), this);
    m_wandOptions->setObjectName(QStringLiteral("WandOptionsBar"));
    m_wandOptions->setMovable(false);
    {
        auto *host = new QWidget(m_wandOptions);
        auto *row = new QHBoxLayout(host);
        row->setContentsMargins(6, 2, 6, 2);
        row->setSpacing(6);
        auto *title = new QLabel(tr("Magic Wand"), host);
        title->setStyleSheet(QStringLiteral("font-weight: 600;"));
        row->addWidget(title);
        row->addSpacing(8);
        row->addWidget(new QLabel(tr("Tolerance"), host));
        auto *slider = new QSlider(Qt::Horizontal, host);
        slider->setRange(0, 100);
        slider->setFixedWidth(120);
        auto *spin = new QSpinBox(host);
        spin->setRange(0, 100);
        spin->setSuffix(tr("%"));
        spin->setKeyboardTracking(false);
        auto *contiguous = new QCheckBox(tr("Contiguous"), host);
        contiguous->setToolTip(tr("Only pixels connected to the one clicked; off selects the colour everywhere"));
        auto *hint = new QLabel(tr("Shift+click adds · Ctrl+click subtracts"), host);
        hint->setEnabled(false);
        row->addWidget(slider);
        row->addWidget(spin);
        row->addWidget(contiguous);
        row->addSpacing(12);
        row->addWidget(hint);
        row->addStretch(1);
        m_wandOptions->addWidget(host);

        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, this, [this, slider](int v) {
            const QSignalBlocker block(slider);
            slider->setValue(v);
            m_wand->setTolerance(v / 100.0);
        });
        connect(contiguous, &QCheckBox::toggled, this, [this](bool on) { m_wand->setContiguous(on); });
        const auto sync = [this, spin, contiguous] {
            spin->setValue(int(std::lround(m_wand->tolerance() * 100.0)));
            contiguous->setChecked(m_wand->contiguous());
        };
        connect(this, &MainWindow::wandSettingsLoaded, this, sync);
        sync();
    }
    addToolBar(Qt::TopToolBarArea, m_wandOptions);

    // The cursor circle follows size changes immediately.
    connect(m_brush, &BrushTool::settingsChanged, m_view, qOverload<>(&QWidget::update));
}

void MainWindow::createDocks()
{
    setDockOptions(AnimatedDocks | AllowTabbedDocks | AllowNestedDocks);

    m_layerPanel = new LayerPanel;
    auto *layersDock = new QDockWidget(tr("Layers"), this);
    layersDock->setObjectName(QStringLiteral("LayersDock"));
    layersDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, layersDock);
    connect(m_layerPanel, &LayerPanel::activated, this, &MainWindow::setActiveLayer);
    connect(m_layerPanel, &LayerPanel::visibilityChanged, this, &MainWindow::setLayerVisible);
    connect(m_layerPanel, &LayerPanel::lockChanged, this, &MainWindow::setLayerLocked);
    connect(m_layerPanel, &LayerPanel::renamed, this, &MainWindow::renameLayer);
    connect(m_layerPanel, &LayerPanel::opacityChanged, this, &MainWindow::setLayerOpacity);
    connect(m_layerPanel, &LayerPanel::blendChanged, this, &MainWindow::setLayerBlend);
    connect(m_layerPanel, &LayerPanel::rearranged, this, &MainWindow::rearrangeLayers);
    connect(m_layerPanel, &LayerPanel::addLayerRequested, this, &MainWindow::addLayer);
    connect(m_layerPanel, &LayerPanel::addGroupRequested, this, &MainWindow::addGroup);
    connect(m_layerPanel, &LayerPanel::duplicateRequested, this, &MainWindow::duplicateLayer);
    connect(m_layerPanel, &LayerPanel::raiseRequested, this, &MainWindow::raiseLayer);
    connect(m_layerPanel, &LayerPanel::lowerRequested, this, &MainWindow::lowerLayer);
    connect(m_layerPanel, &LayerPanel::mergeRequested, this, &MainWindow::mergeDown);
    connect(m_layerPanel, &LayerPanel::deleteRequested, this, &MainWindow::deleteLayer);

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
    m_selectionLabel = new QLabel(this);

    statusBar()->addWidget(m_posLabel, 1);
    statusBar()->addPermanentWidget(m_selectionLabel);
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
    activateTool(m_brush, true);
}

void MainWindow::selectEyedropper()
{
    activateTool(m_eyedropper, false); // brush options don't apply
}

void MainWindow::activateTool(CanvasTool *tool, bool brushOptions)
{
    if (tool != m_move)
        commitFloating();
    m_view->setTool(tool);
    m_options->setEnabled(brushOptions);
    m_options->setVisible(tool != m_wand);
    m_wandOptions->setVisible(tool == m_wand);
    QAction *act = tool == m_rectSelect      ? m_rectSelectAct
                   : tool == m_ellipseSelect ? m_ellipseSelectAct
                   : tool == m_move          ? m_moveAct
                   : tool == m_wand          ? m_wandAct
                   : tool == m_eyedropper    ? m_eyedropperAct
                                             : nullptr;
    if (act)
        act->setChecked(true);
}

// --- Selection, clipboard and moving ------------------------------------------

void MainWindow::setSelection(const easeletch::Selection &selection)
{
    const easeletch::Selection clipped = selection.isEmpty() ? easeletch::Selection() : selection;
    m_selection = clipped;
    m_view->setSelectionOutline(clipped.outlines());
    updateSelectionActions();
}

void MainWindow::updateSelectionActions()
{
    const bool any = !m_selection.isEmpty();
    for (QAction *a : {m_cutAct, m_copyAct, m_deleteAct, m_deselectAct, m_cropAct, m_exportSelectionAct,
                       m_growAct, m_shrinkAct})
        if (a)
            a->setEnabled(any);
    if (m_selectionLabel) {
        const QRect b = m_selection.bounds();
        m_selectionLabel->setText(any ? tr("Selection %1 × %2 at %3, %4")
                                            .arg(b.width()).arg(b.height()).arg(b.x()).arg(b.y())
                                      : QString());
    }
}

void MainWindow::selectAll()
{
    commitFloating();
    setSelection(easeletch::Selection::rect(canvasRect()));
}

void MainWindow::deselect()
{
    commitFloating();
    setSelection({});
}

void MainWindow::storeClip(const QImage &content, const easeletch::Selection &shape, const QPoint &origin)
{
    m_clip.content = content;
    m_clip.shape = shape;
    m_clip.origin = origin;
    m_clip.token = QUuid::createUuid().toByteArray();

    auto *mime = new QMimeData;
    mime->setImageData(easeletch::toClipboardImage(content));
    mime->setData(QString::fromLatin1(kClipMime), m_clip.token);
    QGuiApplication::clipboard()->setMimeData(mime);
}

void MainWindow::copy()
{
    if (!m_stack || m_selection.isEmpty())
        return;
    if (m_floating.isActive()) {
        const easeletch::Selection sel = m_floating.selection();
        storeClip(m_floating.content(), sel.translated(-m_floating.position()), m_floating.position());
        return;
    }
    const easeletch::Selection sel = m_selection;
    const QRect b = sel.bounds();
    storeClip(easeletch::extractSelection(*readStore(), sel), sel.translated(-b.topLeft()), b.topLeft());
}

void MainWindow::cut()
{
    if (!m_stack || m_selection.isEmpty() || m_view->isStroking())
        return;
    if (!m_floating.isActive() && !editStore())
        return;
    copy();
    clearSelected(tr("Cut"));
}

void MainWindow::deleteSelection()
{
    if (!m_stack || m_selection.isEmpty() || m_view->isStroking())
        return;
    clearSelected(tr("Delete"));
}

void MainWindow::clearSelected(const QString &label)
{
    const easeletch::Selection target = m_selection;
    QHash<easeletch::TileCoord, QImage> before;
    int layerId = m_stack->activeId();
    if (m_floating.isActive()) {
        // Floating pixels just go away: for a paste that's no change at all,
        // for a lift it's the original area cleared.
        const bool lifted = m_floating.wasLifted();
        const easeletch::Selection origin = m_selectionBeforeFloat;
        m_floating.cancel();
        layerId = m_floatLayer;
        easeletch::Layer *l = m_stack->layer(layerId);
        if (lifted && l)
            before = easeletch::clearSelection(l->store, origin, canvasRect());
    } else {
        easeletch::TileStore *store = editStore();
        if (!store)
            return;
        before = easeletch::clearSelection(*store, target, canvasRect());
    }
    if (!before.isEmpty())
        m_history.push(label, layerId, std::move(before));
    setSelection(target);
    m_view->refresh();
    historyChanged();
}

QRect MainWindow::visibleCanvasRect() const
{
    const QRectF view = m_view->canvasToView().inverted().mapRect(QRectF(m_view->rect()));
    return view.toAlignedRect() & canvasRect();
}

void MainWindow::paste()
{
    if (!m_stack || m_view->isStroking())
        return;

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    const bool ours = !m_clip.content.isNull() &&
                      (!mime || !mime->hasImage() || mime->data(QString::fromLatin1(kClipMime)) == m_clip.token);
    QImage content;
    easeletch::Selection shape;
    QPoint pos;
    const QRect visible = visibleCanvasRect();
    if (ours) {
        // Lossless, with its shape, back where it came from if that's in view.
        content = m_clip.content;
        shape = m_clip.shape;
        pos = QRect(m_clip.origin, content.size()).intersects(visible)
                  ? m_clip.origin
                  : visible.center() - QPoint(content.width() / 2, content.height() / 2);
    } else if (mime && mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (image.isNull())
            return;
        content = easeletch::fromClipboardImage(image);
        pos = visible.isEmpty() ? QPoint(0, 0)
                                : visible.center() - QPoint(content.width() / 2, content.height() / 2);
    } else {
        return;
    }

    commitFloating();
    easeletch::TileStore *store = editStore();
    if (!store)
        return;
    m_selectionBeforeFloat = m_selection;
    m_floatLayer = m_stack->activeId();
    m_floating.paste(store, content, pos, shape, canvasRect());
    m_floatLabel = tr("Paste");
    m_floatStart = QPoint(INT_MIN, INT_MIN); // always a change
    setSelection(m_floating.selection());
    activateTool(m_move, false);
    m_view->refresh();
}

bool MainWindow::liftForMove()
{
    if (m_floating.isActive())
        return true;
    easeletch::TileStore *store = m_stack ? editStore() : nullptr;
    if (!store)
        return false;
    // With nothing selected, Move takes the whole layer.
    const easeletch::Selection sel = m_selection.isEmpty() ? easeletch::Selection::rect(canvasRect()) : m_selection;
    m_selectionBeforeFloat = m_selection;
    m_floatLayer = m_stack->activeId();
    m_floating.lift(store, sel, canvasRect());
    if (!m_floating.isActive())
        return false;
    m_floatLabel = tr("Move");
    m_floatStart = m_floating.position();
    setSelection(m_floating.selection());
    return true;
}

void MainWindow::moveDragStarted(const QPointF &pos)
{
    m_moveDragging = liftForMove();
    m_dragStart = pos;
    m_dragOrigin = m_floating.position();
}

void MainWindow::moveDragged(const QPointF &pos)
{
    if (!m_moveDragging || !m_floating.isActive())
        return;
    const QPointF d = pos - m_dragStart;
    m_floating.moveTo(m_dragOrigin + QPoint(int(std::lround(d.x())), int(std::lround(d.y()))));
    setSelection(m_floating.selection());
    m_view->refresh();
}

void MainWindow::nudge(const QPoint &delta)
{
    if (m_moveDragging || !liftForMove())
        return;
    m_floating.moveBy(delta);
    setSelection(m_floating.selection());
    m_view->refresh();
}

void MainWindow::commitFloating()
{
    if (!m_floating.isActive())
        return;
    if (m_floating.position() == m_floatStart) {
        // Lifted and put back where it was: nothing to record.
        cancelFloating();
        return;
    }
    const easeletch::Selection placed = m_floating.selection();
    m_history.push(m_floatLabel, m_floatLayer, m_floating.commit());
    m_moveDragging = false;
    setSelection(placed);
    m_view->refresh();
    historyChanged();
}

void MainWindow::setGridSettings(const GridSettings &grid)
{
    m_grid = grid;
    m_view->setPixelGridVisible(grid.pixel);
    m_view->setCellGrid(grid.cells, grid.cell, grid.offset);
    const bool snap = grid.cells && grid.snap;
    for (SelectTool *t : {m_rectSelect, m_ellipseSelect})
        t->setSnapGrid(snap ? grid.cell : QSize(), grid.offset);
    for (auto [act, on] : {std::pair{m_pixelGridAct, grid.pixel}, std::pair{m_cellGridAct, grid.cells}}) {
        if (act) {
            const QSignalBlocker block(act);
            act->setChecked(on);
        }
    }
}

void MainWindow::showGridDialog()
{
    const GridSettings before = m_grid;
    GridDialog dlg(m_grid, this);
    connect(&dlg, &GridDialog::settingsChanged, this, &MainWindow::setGridSettings);
    setGridSettings(dlg.settings()); // opening the settings shows the grid
    if (dlg.exec() == QDialog::Accepted)
        setGridSettings(dlg.settings());
    else
        setGridSettings(before);
}

void MainWindow::cropToSelection()
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    if (m_selection.isEmpty())
        return;
    const QRect rect = m_selection.bounds() & canvasRect();
    cropCanvasTo(rect, tr("Crop to %1 × %2").arg(rect.width()).arg(rect.height()));
}

void MainWindow::trim()
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    // What you see decides: every visible layer counts.
    syncComposite();
    const QRect rect = easeletch::opaqueBounds(m_stack->composite(), canvasRect());
    if (rect.isEmpty()) {
        statusBar()->showMessage(tr("Nothing to trim to: the canvas is fully transparent"), 4000);
        return;
    }
    if (rect == canvasRect()) {
        statusBar()->showMessage(tr("No transparent edges to trim"), 4000);
        return;
    }
    cropCanvasTo(rect, tr("Trim to %1 × %2").arg(rect.width()).arg(rect.height()));
}

void MainWindow::cropCanvasTo(const QRect &rect, const QString &label)
{
    if (rect.isEmpty() || rect == canvasRect())
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    for (const easeletch::Layer &l : m_stack->layers())
        if (!l.group)
            easeletch::cropStore(m_stack->layer(l.id)->store, rect);
    m_stack->setSize(rect.size());
    m_history.pushState(label, std::move(before), *m_stack);
    setSelection({});
    m_stack->recompositeAll();
    applyCanvasSize();
    historyChanged();
}

void MainWindow::invertSelection()
{
    commitFloating();
    setSelection(m_selection.inverted(canvasRect()));
}

void MainWindow::growSelection(int pixels)
{
    if (m_selection.isEmpty())
        return;
    commitFloating();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const easeletch::Selection s = m_selection.grown(pixels, canvasRect());
    QApplication::restoreOverrideCursor();
    setSelection(s);
}

void MainWindow::wandClicked(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    if (!m_stack)
        return;
    const QPoint seed(int(std::floor(pos.x())), int(std::floor(pos.y())));
    if (!canvasRect().contains(seed))
        return;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const easeletch::Selection picked =
        easeletch::magicWand(*readStore(), canvasRect(), seed, m_wand->tolerance(), m_wand->contiguous());
    QApplication::restoreOverrideCursor();
    if (modifiers & Qt::ShiftModifier)
        setSelection(m_selection.united(picked));
    else if (modifiers & Qt::ControlModifier)
        setSelection(m_selection.subtracted(picked));
    else
        setSelection(picked);
}

void MainWindow::colorToAlpha(const QColor &color, double threshold)
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    easeletch::TileStore *store = editStore();
    if (!store)
        return;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    auto before = easeletch::colorToAlpha(*store, m_selection, canvasRect(), color, threshold);
    QApplication::restoreOverrideCursor();
    if (before.isEmpty())
        return;
    m_history.push(tr("Color to Alpha"), m_stack->activeId(), std::move(before));
    m_view->refresh();
    historyChanged();
}

void MainWindow::showColorToAlphaDialog()
{
    if (!m_stack || !editStore())
        return;
    const QColor corner = easeletch::pixelToColor(readStore()->pixel(0, 0));
    ColorToAlphaDialog dlg(m_brush->color(), corner.alpha() > 0 ? corner : QColor(), m_colorToAlphaThreshold, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_colorToAlphaThreshold = dlg.threshold();
    colorToAlpha(dlg.color(), dlg.threshold());
}

void MainWindow::applyCanvasSize()
{
    const QRect bounds = canvasRect();
    m_view->setDocument(m_stack->compositeStore(), canvasSize());
    bindTools();
    m_layerPanel->setStack(m_stack.get());
    if (!m_selection.isEmpty() && !m_selection.bounds().intersects(bounds))
        setSelection({});
    updateTitle();
}

void MainWindow::afterHistoryMove(const QSize &sizeBefore, bool stackChanged)
{
    if (stackChanged) {
        // The layer list was swapped for another: everything that pointed
        // into it is rebound.
        m_stack->recompositeAll();
        if (canvasSize() != sizeBefore) {
            applyCanvasSize();
        } else {
            bindTools();
            m_layerPanel->setStack(m_stack.get());
            m_view->refresh();
        }
    } else {
        m_view->refresh();
    }
    historyChanged();
}

bool MainWindow::exportSelectionTo(const QString &path)
{
    if (!m_stack || m_selection.isEmpty())
        return false;
    syncComposite();
    const easeletch::Selection sel = m_floating.isActive() ? m_floating.selection() : m_selection;
    const QRect b = sel.bounds();
    const QRect keep = b & canvasRect();
    if (keep.isEmpty())
        return false;
    const QImage image = easeletch::toClipboardImage(easeletch::extractSelection(*readStore(), sel))
                             .copy(keep.translated(-b.topLeft()));
    const QString error = easeletch::writeImageFile(path, image);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Export Selection"),
                             tr("Could not export %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

void MainWindow::showExportSelectionDialog()
{
    if (!m_stack || m_selection.isEmpty())
        return;
    const QRect b = m_selection.bounds();
    const QString suggested = QDir(m_lastDir).filePath(
        QStringLiteral("%1_%2_%3.png").arg(QFileInfo(m_name).completeBaseName()).arg(b.x()).arg(b.y()));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Selection"), suggested,
                                                tr("PNG image (*.png);;WebP image (*.webp)"));
    if (path.isEmpty())
        return;
    path = withSuffix(path, QStringLiteral("png"));
    m_lastDir = QFileInfo(path).absolutePath();
    exportSelectionTo(path);
}

void MainWindow::cancelFloating()
{
    if (!m_floating.isActive())
        return;
    m_floating.cancel();
    m_moveDragging = false;
    setSelection(m_selectionBeforeFloat);
    m_view->refresh();
}

// --- Layers ---------------------------------------------------------------------

easeletch::TileStore *MainWindow::layer() const
{
    easeletch::Layer *l = m_stack ? m_stack->active() : nullptr;
    return l && !l->group ? &l->store : nullptr;
}

easeletch::TileStore *MainWindow::editStore()
{
    easeletch::Layer *l = m_stack ? m_stack->active() : nullptr;
    if (!l)
        return nullptr;
    QString why;
    if (l->group)
        why = tr("A group has no pixels of its own: pick a layer inside it");
    else if (m_stack->isLocked(l->id))
        why = tr("\"%1\" is locked").arg(l->name);
    else if (!m_stack->isShown(l->id))
        why = tr("\"%1\" is hidden").arg(l->name);
    if (!why.isEmpty()) {
        statusBar()->showMessage(why, 4000);
        return nullptr;
    }
    return &l->store;
}

const easeletch::TileStore *MainWindow::readStore() const
{
    if (const easeletch::TileStore *store = layer())
        return store;
    syncComposite();
    return &m_stack->composite();
}

void MainWindow::bindTools()
{
    const QRect bounds = canvasRect();
    easeletch::Layer *l = m_stack->active();
    const bool paintable = l && !l->group && !m_stack->isLocked(l->id) && m_stack->isShown(l->id);
    m_brush->setDocument(paintable ? &l->store : nullptr, bounds, &m_history, l ? l->id : 0);
    // The eyedropper picks what's on screen.
    m_eyedropper->setDocument(m_stack->compositeStore(), bounds);
}

bool MainWindow::beginLayerChange()
{
    if (!m_stack || m_view->isStroking())
        return false;
    commitFloating();
    return true;
}

void MainWindow::finishLayerChange(const QString &label, easeletch::LayerStack before)
{
    m_history.pushState(label, std::move(before), *m_stack);
    layersChanged();
}

void MainWindow::layersChanged()
{
    m_opacityRedrawPending = false; // covered by this
    m_stack->recompositeAll();
    bindTools();
    m_layerPanel->setStack(m_stack.get());
    m_view->refresh();
    historyChanged();
}

void MainWindow::setActiveLayer(int id)
{
    if (!m_stack || id == m_stack->activeId() || !m_stack->layer(id) || m_view->isStroking())
        return;
    commitFloating();
    m_stack->setActive(id);
    bindTools();
    m_layerPanel->syncActive();
}

bool MainWindow::addLayer()
{
    if (!beginLayerChange())
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    easeletch::Layer l;
    l.name = m_stack->uniqueName(tr("Layer"));
    const easeletch::Layer *active = m_stack->active();
    int id = 0;
    if (active && active->group) {
        id = m_stack->insert(std::move(l), active->id, INT_MAX); // on top, inside the group
    } else {
        const int parent = active ? active->parent : 0;
        const int at = active ? int(m_stack->children(parent).indexOf(active->id)) + 1 : INT_MAX;
        id = m_stack->insert(std::move(l), parent, at);
    }
    m_stack->setActive(id);
    finishLayerChange(tr("New Layer"), std::move(before));
    return true;
}

bool MainWindow::addGroup()
{
    if (!beginLayerChange() || !m_stack->active())
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    const int activeId = m_stack->activeId();
    const int parent = m_stack->active()->parent;
    easeletch::Layer g;
    g.group = true;
    g.name = m_stack->uniqueName(tr("Group"));
    const int group = m_stack->insert(std::move(g), parent, int(m_stack->children(parent).indexOf(activeId)));
    m_stack->move(activeId, group, 0);
    m_stack->setActive(activeId);
    finishLayerChange(tr("Group Layer"), std::move(before));
    return true;
}

bool MainWindow::duplicateLayer()
{
    if (!beginLayerChange() || !m_stack->active())
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->setActive(m_stack->duplicate(m_stack->activeId()));
    finishLayerChange(tr("Duplicate Layer"), std::move(before));
    return true;
}

bool MainWindow::deleteLayer()
{
    if (!beginLayerChange() || !m_stack->active())
        return false;
    const int id = m_stack->activeId();
    if (m_stack->subtree(id).size() >= m_stack->count()) {
        statusBar()->showMessage(tr("A document needs at least one layer"), 4000);
        return false;
    }
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->remove(id);
    finishLayerChange(tr("Delete Layer"), std::move(before));
    return true;
}

bool MainWindow::mergeDown()
{
    if (!beginLayerChange() || !m_stack->active())
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    const bool group = m_stack->active()->group;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const bool ok = group ? m_stack->mergeGroup(m_stack->activeId()) : m_stack->mergeDown(m_stack->activeId());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        statusBar()->showMessage(tr("There's no layer directly below to merge into"), 4000);
        return false;
    }
    finishLayerChange(group ? tr("Merge Group") : tr("Merge Down"), std::move(before));
    return true;
}

bool MainWindow::flattenImage()
{
    if (!beginLayerChange() || m_stack->count() < 2)
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    m_stack->flatten(tr("Background"));
    QApplication::restoreOverrideCursor();
    finishLayerChange(tr("Flatten Image"), std::move(before));
    return true;
}

bool MainWindow::moveLayerBy(int step)
{
    if (!beginLayerChange() || !m_stack->active())
        return false;
    const easeletch::Layer *l = m_stack->active();
    const int id = l->id;
    const QList<int> siblings = m_stack->children(l->parent);
    const int at = int(siblings.indexOf(id));
    const int to = at + step;
    easeletch::LayerStack before = m_stack->snapshot();
    if (to >= 0 && to < siblings.size()) {
        m_stack->move(id, l->parent, to);
    } else if (l->parent != 0) {
        // Past the end of its group: out of it, just above or below the group.
        const easeletch::Layer *group = m_stack->layer(l->parent);
        const int outer = group->parent;
        const int groupAt = int(m_stack->children(outer).indexOf(group->id));
        m_stack->move(id, outer, step > 0 ? groupAt + 1 : groupAt);
    } else {
        return false;
    }
    finishLayerChange(tr("Move Layer"), std::move(before));
    return true;
}

bool MainWindow::raiseLayer()
{
    return moveLayerBy(1);
}

bool MainWindow::lowerLayer()
{
    return moveLayerBy(-1);
}

bool MainWindow::rearrangeLayers(const QList<QPair<int, int>> &order)
{
    QList<QPair<int, int>> current;
    if (m_stack) {
        for (const easeletch::Layer &l : m_stack->layers())
            current.append({l.id, l.parent});
    }
    easeletch::LayerStack before;
    const bool ok = order != current && beginLayerChange()
                    && (before = m_stack->snapshot(), m_stack->rearrange(order));
    if (!ok) {
        if (m_stack)
            m_layerPanel->setStack(m_stack.get()); // put the panel back as the document is
        return false;
    }
    finishLayerChange(tr("Move Layer"), std::move(before));
    return true;
}

void MainWindow::setLayerVisible(int id, bool visible)
{
    if (!beginLayerChange() || !m_stack->layer(id) || m_stack->layer(id)->visible == visible)
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->layer(id)->visible = visible;
    finishLayerChange(visible ? tr("Show Layer") : tr("Hide Layer"), std::move(before));
}

void MainWindow::setLayerLocked(int id, bool locked)
{
    if (!beginLayerChange() || !m_stack->layer(id) || m_stack->layer(id)->locked == locked)
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->layer(id)->locked = locked;
    finishLayerChange(locked ? tr("Lock Layer") : tr("Unlock Layer"), std::move(before));
}

void MainWindow::renameLayer(int id, const QString &name)
{
    if (!beginLayerChange() || !m_stack->layer(id) || name.isEmpty() || m_stack->layer(id)->name == name)
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->layer(id)->name = name;
    finishLayerChange(tr("Rename Layer"), std::move(before));
}

void MainWindow::setLayerBlend(int id, easeletch::BlendMode mode)
{
    if (!beginLayerChange() || !m_stack->layer(id) || m_stack->layer(id)->blend == mode)
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->layer(id)->blend = mode;
    finishLayerChange(tr("Blend Mode: %1").arg(LayerPanel::blendModeName(mode)), std::move(before));
}

void MainWindow::setLayerOpacity(int id, double opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (!beginLayerChange() || !m_stack->layer(id) || m_stack->layer(id)->opacity == opacity)
        return;
    // Dragging the slider sends many values; they add up to one undo step.
    const bool continuing = m_opacityLayer == id && m_opacityState == m_history.stateId();
    easeletch::LayerStack before;
    if (!continuing)
        before = m_stack->snapshot();
    m_stack->layer(id)->opacity = opacity;
    if (!continuing) {
        m_history.pushState(tr("Layer Opacity"), std::move(before), *m_stack);
        m_opacityLayer = id;
        m_opacityState = m_history.stateId();
        historyChanged();
    }
    // Redraw once the slider's queued moves are in, not once per move: on a
    // large canvas a redraw takes longer than the moves arrive.
    if (!m_opacityRedrawPending) {
        m_opacityRedrawPending = true;
        QTimer::singleShot(0, this, &MainWindow::flushOpacityRedraw);
    }
}

void MainWindow::flushOpacityRedraw()
{
    if (m_opacityRedrawPending && m_stack)
        m_view->refresh(); // brings the composite up to date first
}

void MainWindow::syncComposite() const
{
    if (m_opacityRedrawPending) {
        m_opacityRedrawPending = false;
        m_stack->recompositeAll();
    } else {
        m_stack->updateComposite();
    }
}

void MainWindow::newDocument(const QSize &size, const QColor &background)
{
    auto stack = std::make_unique<easeletch::LayerStack>(
        easeletch::LayerStack::single(easeletch::TileStore(background), size, tr("Background")));
    setDocument(std::move(stack), tr("Untitled"), tr("New %1 × %2").arg(size.width()).arg(size.height()));
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
        auto doc = std::make_shared<easeletch::LoadedDocument>(easeletch::loadDocument(path));
        QMetaObject::invokeMethod(
            qApp,
            [self, path, generation, doc] {
                if (self)
                    self->finishOpen(path, generation, std::move(*doc));
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::finishOpen(const QString &path, quint64 generation, easeletch::LoadedDocument doc)
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
    // Only an .easeletch file is saved back to; an opened image gets Save As.
    setDocument(std::move(doc.stack), info.fileName(), tr("Open %1").arg(info.fileName()),
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
    const QString suffix = QLatin1String(easeletch::NativeSuffix);
    QString suggested = m_path;
    if (suggested.isEmpty())
        suggested = QDir(m_lastDir).filePath(QFileInfo(m_name).completeBaseName() + QLatin1Char('.') + suffix);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested,
                                                      tr("Easeletch documents (*.%1)").arg(suffix));
    return path.isEmpty() ? QString() : withSuffix(path, suffix);
}

bool MainWindow::saveDocumentTo(const QString &path, bool wait)
{
    if (!m_stack)
        return false;
    commitFloating();
    // The copy shares tiles with the live document (copy-on-write), so
    // painting can go on while it's written out.
    syncComposite();
    m_opacityLayer = 0; // a later opacity change is a change since this save
    easeletch::LayerStack snapshot = *m_stack;
    const quint64 stateId = m_history.stateId();
    const quint64 docGeneration = m_docGeneration;
    m_lastDir = QFileInfo(path).absolutePath();

    if (wait) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        const QString error = easeletch::saveNativeDocument(path, std::move(snapshot));
        QApplication::restoreOverrideCursor();
        ++m_pendingJobs; // balanced in finishSave
        finishSave(path, stateId, docGeneration, error);
        return error.isEmpty();
    }

    statusBar()->showMessage(tr("Saving %1…").arg(QFileInfo(path).fileName()));
    ++m_pendingJobs;
    QPointer<MainWindow> self(this);
    auto shared = std::make_shared<easeletch::LayerStack>(std::move(snapshot));
    QThreadPool::globalInstance()->start([self, path, shared, stateId, docGeneration] {
        const QString error = easeletch::saveNativeDocument(path, std::move(*shared));
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
    commitFloating();
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

void MainWindow::setDocument(std::unique_ptr<easeletch::LayerStack> stack, const QString &name,
                             const QString &historyLabel, easeletch::TilePyramid pyramid, const QString &path)
{
    // Detach everything from the old document before it's freed.
    m_floating.cancel();
    m_moveDragging = false;
    setSelection({});
    m_brush->setDocument(nullptr, {}, nullptr);
    m_eyedropper->setDocument(nullptr, {});
    m_layerPanel->setStack(nullptr);
    m_history.reset(historyLabel);
    m_cleanId = m_history.stateId();
    ++m_docGeneration;
    m_path = path;

    m_view->setDocument(nullptr, {});
    m_stack = std::move(stack);
    m_name = name;
    m_opacityLayer = 0;
    m_view->setDocument(m_stack->compositeStore(), canvasSize(), std::move(pyramid));
    bindTools();
    m_layerPanel->setStack(m_stack.get());

    historyChanged();
    updateTitle();
    updateMemoryLabel();
}

void MainWindow::undo()
{
    if (!m_stack || m_view->isStroking())
        return;
    if (m_floating.isActive()) {
        // Undoing an uncommitted move or paste is just putting it back.
        cancelFloating();
        return;
    }
    if (!m_history.canUndo())
        return;
    const QSize before = canvasSize();
    afterHistoryMove(before, m_history.undo(*m_stack));
}

void MainWindow::redo()
{
    if (!m_stack || m_view->isStroking() || m_floating.isActive() || !m_history.canRedo())
        return;
    const QSize before = canvasSize();
    afterHistoryMove(before, m_history.redo(*m_stack));
}

void MainWindow::historyItemClicked(QListWidgetItem *item)
{
    if (!m_stack || m_view->isStroking())
        return;
    cancelFloating(); // like undo: jumping in history drops an uncommitted move
    const QSize before = canvasSize();
    afterHistoryMove(before, m_history.jumpTo(m_historyList->row(item), *m_stack));
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
    setWindowTitle(tr("%1[*] (%2 × %3) — Easeletch").arg(m_name).arg(canvasSize().width()).arg(canvasSize().height()));
    setWindowModified(isModified());
}

void MainWindow::updateMemoryLabel()
{
    const qint64 bytes = m_stack ? m_stack->memoryBytes() : 0;
    const qsizetype tiles = m_stack ? m_stack->tileCount() : 0;
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
    NewDocumentDialog dlg(canvasSize().isEmpty() ? QSize(2000, 1500) : canvasSize(), this);
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
    if (!m_stack)
        return;
    commitFloating();
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
    syncComposite();
    auto snapshot = std::make_shared<easeletch::TileStore>(m_stack->composite().snapshot());
    const QSize size = canvasSize();
    QThreadPool::globalInstance()->start([self, path, snapshot, size] {
        const QString error = easeletch::exportImage(path, *snapshot, size);
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
    QMessageBox::about(this, tr("About Easeletch"),
                       tr("<h3>Easeletch %1</h3>"
                          "<p>A balanced layered image editor.</p>"
                          "<p>Free software under the GNU General Public License, version 3.</p>"
                          "<p>Built with Qt %2, running on Qt %3.</p>"
                          "<p><a href=\"https://github.com/scottpeterman/easel\">github.com/scottpeterman/easel</a></p>")
                           .arg(QStringLiteral(EASELETCH_VERSION), QStringLiteral(QT_VERSION_STR),
                                QString::fromLatin1(qVersion())));
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
    m_grid.save(settings);
    settings.setValue(QStringLiteral("wand/tolerance"), m_wand->tolerance());
    settings.setValue(QStringLiteral("wand/contiguous"), m_wand->contiguous());
    event->accept();
}
