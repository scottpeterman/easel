#include "mainwindow.h"

#include "adjustpanel.h"
#include "brushoptionsbar.h"
#include "brushtool.h"
#include "canvasarea.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "colortoalphadialog.h"
#include "documentio.h"
#include "edittools.h"
#include "eyedroppertool.h"
#include "filterdialog.h"
#include "layerpanel.h"
#include "newdocumentdialog.h"
#include "selecttools.h"
#include "textpanel.h"
#include "textrender.h"

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
#include <QLineF>
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

#include <algorithm>
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
    createPageBar(); // the canvas, with the page tabs under it

    m_brush = new BrushTool(this);
    m_eyedropper = new EyedropperTool(this);
    m_rectSelect = new SelectTool(easeletch::Selection::Shape::Rect, this);
    m_ellipseSelect = new SelectTool(easeletch::Selection::Shape::Ellipse, this);
    m_move = new MoveTool(this);
    m_wand = new WandTool(this);
    connect(m_wand, &WandTool::clicked, this, &MainWindow::wandClicked);
    m_textTool = new TextTool(this);
    connect(m_textTool, &TextTool::pressed, this, &MainWindow::textPressed);
    connect(m_textTool, &TextTool::dragged, this, &MainWindow::textDragged);
    connect(m_textTool, &TextTool::released, this, &MainWindow::textReleased);
    m_textPanel = new TextPanel(this);
    connect(m_textPanel, &TextPanel::changed, this, &MainWindow::updateText);
    connect(m_textPanel, &QDialog::accepted, this, &MainWindow::commitText);
    connect(m_textPanel, &QDialog::rejected, this, &MainWindow::cancelText);
    m_fillTool = new FillTool(this);
    connect(m_fillTool, &FillTool::clicked, this, [this](const QPointF &pos) {
        fillAt(QPoint(int(std::floor(pos.x())), int(std::floor(pos.y()))));
    });
    m_gradientTool = new GradientTool(this);
    // While it's dragged, a line shows where the gradient will run.
    connect(m_gradientTool, &GradientTool::dragged, this, [this](const QPointF &from, const QPointF &to) {
        QList<QPolygonF> lines = m_selection.outlines();
        lines << QPolygonF({from, to});
        m_view->setSelectionOutline(lines);
    });
    connect(m_gradientTool, &GradientTool::finished, this, &MainWindow::drawGradient);
    m_transformTool = new TransformTool(this);
    connect(m_transformTool, &TransformTool::pressed, this, &MainWindow::transformPressed);
    connect(m_transformTool, &TransformTool::dragged, this, &MainWindow::transformDragged);
    connect(m_transformTool, &TransformTool::released, this, &MainWindow::transformReleased);
    connect(m_transformTool, &TransformTool::nudged, this, [this](const QPoint &delta) {
        if (!m_xf.active || m_xf.dragging)
            return;
        m_xf.box.center += QPointF(delta);
        applyTransform(true);
    });
    m_lasso = new LassoTool(this);
    connect(m_lasso, &LassoTool::finished, this, &MainWindow::lassoFinished);
    // While it's being drawn, the path shows in place of the selection outline.
    connect(m_lasso, &LassoTool::pathChanged, this, [this](const QPolygonF &path) {
        if (path.isEmpty())
            m_view->setSelectionOutline(m_selection.outlines());
        else
            m_view->setSelectionOutline({path});
    });
    m_brush->setSelection(&m_selection);
    connect(m_brush, &BrushTool::blocked, this, [this] { editStore(); });
    connect(m_brush, &BrushTool::outsideSelection, this, [this] {
        statusBar()->showMessage(tr("Painting stays inside the selection. Ctrl+D deselects, to paint anywhere."),
                                 5000);
    });
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
    m_cloneSource = new CloneSourceTool(m_brush, this);
    connect(m_brush, &BrushTool::cloneSourceNeeded, this, [this] {
        statusBar()->showMessage(tr("Clone: hold Alt and click what to copy first, then paint."), 5000);
    });
    connect(m_brush, &BrushTool::cloneSourceChanged, this, &MainWindow::updateCloneTool);

    createDocks();
    createToolBars();
    createActions();
    createStatusBar();

    QSettings settings;
    m_lastDir = settings.value(QStringLiteral("lastDir")).toString();
    m_brush->loadSettings(settings);
    m_textPanel->loadSettings(settings);
    m_color->loadSettings(settings);
    GridSettings grid;
    grid.load(settings);
    setGridSettings(grid);
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray(), kSettingsVersion);
    m_wandOptions->hide(); // shown with the wand
    m_transformOptions->hide(); // ... and with a transform
    m_fillOptions->hide();
    m_gradientOptions->hide();
    m_fill.tolerance = settings.value(QStringLiteral("fill/tolerance"), m_fill.tolerance).toDouble();
    m_fill.contiguous = settings.value(QStringLiteral("fill/contiguous"), m_fill.contiguous).toBool();
    m_fill.allLayers = settings.value(QStringLiteral("fill/allLayers"), m_fill.allLayers).toBool();
    syncFillOptions();
    m_gradient.shape = easeletch::gradientShapeFromKey(settings.value(QStringLiteral("gradient/shape")).toString());
    m_gradient.preset = settings.value(QStringLiteral("gradient/preset"), m_gradient.preset).toString();
    m_gradient.custom = easeletch::stopsFromString(settings.value(QStringLiteral("gradient/custom")).toString());
    for (const QString &entry : settings.value(QStringLiteral("gradient/saved")).toStringList()) {
        // "name<tab>stops"
        const qsizetype tab = entry.indexOf(QLatin1Char('\t'));
        if (tab > 0)
            saveUserGradient(entry.left(tab), easeletch::stopsFromString(entry.mid(tab + 1)));
    }
    m_gradient.reverse = settings.value(QStringLiteral("gradient/reverse"), m_gradient.reverse).toBool();
    const QColor end(settings.value(QStringLiteral("gradient/end"), m_gradient.end.name()).toString());
    if (end.isValid())
        m_gradient.end = end;
    setGradientOptions(m_gradient);
    setTransformSmooth(settings.value(QStringLiteral("transform/smooth"), true).toBool());
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
    auto *fillAct = edit->addAction(tr("&Fill with Colour"), this, &MainWindow::fillSelection);
    fillAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    fillAct->setToolTip(tr("Fill the selection, or the whole layer, with the current colour"));
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
    m_featherAct = edit->addAction(tr("&Feather Selection..."), this, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Feather Selection"), tr("Fade the edge over (pixels):"),
                                           m_featherPixels, 1, 250, 1, &ok);
        if (ok) {
            m_featherPixels = n;
            featherSelection(n);
        }
    });
    m_featherAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
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
    QMenu *adjust = layer->addMenu(tr("New &Adjustment Layer"));
    for (int t = 1; t < easeletch::AdjustmentTypeCount; ++t) {
        const auto type = easeletch::AdjustmentType(t);
        adjust->addAction(AdjustPanel::typeName(type), this, [this, type] { addAdjustmentLayer(type); });
    }
    layer->addAction(tr("D&elete Layer"), this, &MainWindow::deleteLayer);
    layer->addSeparator();
    layer->addAction(tr("&Shade Areas..."), this, &MainWindow::showShadeDialog)
        ->setStatusTip(tr("A gradient on every enclosed area of a line drawing, on a Multiply layer above it"));
    layer->addSeparator();
    layer->addAction(tr("Move &Up"), this, &MainWindow::raiseLayer);
    layer->addAction(tr("Move D&own"), this, &MainWindow::lowerLayer);
    layer->addSeparator();
    auto *mergeAct = layer->addAction(tr("&Merge Down"), this, &MainWindow::mergeDown);
    mergeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    layer->addAction(tr("&Flatten Image"), this, &MainWindow::flattenImage);
    layer->addSeparator();
    // The selected pixels, or everything on the layer when nothing is selected.
    layer->addAction(m_transformAct);
    layer->addAction(tr("Flip &Horizontal"), this, &MainWindow::flipHorizontal);
    layer->addAction(tr("Flip &Vertical"), this, &MainWindow::flipVertical);
    layer->addAction(tr("Rotate 90° &Right"), this, [this] { rotateQuarter(1); });
    layer->addAction(tr("Rotate 90° &Left"), this, [this] { rotateQuarter(-1); });
    layer->addAction(tr("Rotate &180°"), this, [this] { rotateQuarter(2); });
    layer->addSeparator();
    layer->addAction(tr("Add Layer Mas&k"), this, &MainWindow::addLayerMask);
    m_editMaskAct = layer->addAction(tr("&Paint on Mask"));
    m_editMaskAct->setCheckable(true);
    m_editMaskAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(m_editMaskAct, &QAction::triggered, this, &MainWindow::setEditingMask);
    layer->addAction(tr("&Apply Layer Mask"), this, &MainWindow::applyLayerMask);
    layer->addAction(tr("&Remove Layer Mask"), this, &MainWindow::deleteLayerMask);

    QMenu *filter = menuBar()->addMenu(tr("Filte&r"));
    m_repeatFilterAct = filter->addAction(tr("&Repeat Last Filter"), this, &MainWindow::repeatFilter);
    m_repeatFilterAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
    m_repeatFilterAct->setEnabled(false);
    filter->addSeparator();
    for (int t = 0; t < easeletch::FilterTypeCount; ++t) {
        const auto type = easeletch::FilterType(t);
        m_lastFilters[t] = easeletch::Filter::make(type);
        if (type == easeletch::FilterType::PencilSketch)
            filter->addSeparator(); // the two that redraw the picture, apart from the rest
        filter->addAction(tr("%1...").arg(FilterDialog::typeName(type)), this, [this, type] { showFilterDialog(type); });
    }

    createPageMenu();

    auto *smaller = edit->addAction(tr("Smaller Brush"), this, [this] { m_brush->scaleSize(1.0 / 1.2); });
    smaller->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    auto *larger = edit->addAction(tr("Larger Brush"), this, [this] { m_brush->scaleSize(1.2); });
    larger->setShortcut(QKeySequence(Qt::Key_BracketRight));

    // Enter drops floating pixels, Escape puts them back (or deselects). Only
    // while the canvas has focus, so they don't steal keys from dialogs and fields.
    auto *commitAct = new QAction(tr("Commit"), m_view);
    commitAct->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    commitAct->setShortcutContext(Qt::WidgetShortcut);
    connect(commitAct, &QAction::triggered, this, [this] {
        if (m_lasso->isOpen())
            m_lasso->finish();
        else
            commitFloating();
    });
    m_view->addAction(commitAct);
    auto *escapeAct = new QAction(tr("Cancel"), m_view);
    escapeAct->setShortcut(QKeySequence(Qt::Key_Escape));
    escapeAct->setShortcutContext(Qt::WidgetShortcut);
    connect(escapeAct, &QAction::triggered, this, [this] {
        if (m_lasso->isOpen())
            m_lasso->cancel();
        else if (m_floating.isActive() || m_text.active)
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
        if (bar != m_wandOptions && bar != m_options && bar != m_transformOptions && bar != m_fillOptions
            && bar != m_gradientOptions) // these follow the tool
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
    m_cloneAct = tools->addAction(tr("Clone"), this, [this] { selectBrushMode(int(BrushMode::Clone)); });
    m_cloneAct->setShortcut(QKeySequence(Qt::Key_C));
    m_cloneAct->setToolTip(tr("Clone (C): hold Alt and click what to copy, then paint it somewhere else. "
                              "Later strokes carry on the same copy."));
    m_healAct = tools->addAction(tr("Heal"), this, [this] { selectBrushMode(int(BrushMode::Heal)); });
    m_healAct->setShortcut(QKeySequence(Qt::Key_H));
    m_healAct->setToolTip(tr("Spot heal (H): dab or drag over a blemish. "
                             "It's replaced with what's around it when you let go."));
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
    m_lassoAct = tools->addAction(tr("Lasso"), this, [this] { activateTool(m_lasso, false); });
    m_lassoAct->setShortcut(QKeySequence(Qt::Key_L));
    m_lassoAct->setToolTip(tr("Lasso (L): drag around something, or click point by point and press Enter. "
                              "Shift adds, Ctrl subtracts."));
    m_wandAct = tools->addAction(tr("Wand"), this, [this] { activateTool(m_wand, false); });
    m_wandAct->setShortcut(QKeySequence(Qt::Key_W));
    m_wandAct->setToolTip(tr("Magic wand (W): select by colour. Shift adds, Ctrl subtracts."));
    m_textAct = tools->addAction(tr("Text"), this, [this] { activateTool(m_textTool, false); });
    m_textAct->setShortcut(QKeySequence(Qt::Key_T));
    m_textAct->setToolTip(tr("Text (T): click where it goes, type, drag to move it, then Place. "
                             "Click text already placed to change it. "
                             "It lands on a new layer."));
    m_moveAct = tools->addAction(tr("Move"), this, [this] { activateTool(m_move, false); });
    m_moveAct->setShortcut(QKeySequence(Qt::Key_V));
    m_moveAct->setToolTip(tr("Move selected pixels (V). Arrows nudge 1 px, Shift+arrows 10. "
                             "Enter drops, Escape cancels."));
    tools->addSeparator();
    m_fillAct = tools->addAction(tr("Fill"), this, [this] { activateTool(m_fillTool, false); });
    m_fillAct->setShortcut(QKeySequence(Qt::Key_G));
    m_fillAct->setToolTip(tr("Fill (G): click to flood an area of similar colour with the current colour. "
                             "Stays inside the selection."));
    m_gradientAct = tools->addAction(tr("Gradient"), this, [this] { activateTool(m_gradientTool, false); });
    m_gradientAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_G));
    m_gradientAct->setToolTip(tr("Gradient (Shift+G): drag from where it starts to where it ends. "
                                 "Fills the selection, or the whole layer. Shift keeps the line to 45° steps."));
    m_transformAct = tools->addAction(tr("Transform"), this, [this] {
        // Nothing to transform: the button of the tool still in use stays down.
        if (!beginTransform())
            if (QAction *a = actionFor(m_view->tool()))
                a->setChecked(true);
    });
    m_transformAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_transformAct->setToolTip(tr("Free transform (Ctrl+T): scale, rotate and move the selection, or the whole layer. "
                                  "Enter applies, Escape cancels."));
    for (QAction *a : {m_brushAct, m_eraserAct, m_smudgeAct, m_cloneAct, m_healAct, m_eyedropperAct, m_rectSelectAct,
                       m_ellipseSelectAct, m_lassoAct, m_wandAct, m_textAct, m_moveAct, m_transformAct, m_fillAct,
                       m_gradientAct}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    m_brushAct->setChecked(true);
    connect(m_brush, &BrushTool::modeChanged, this, [this](BrushMode mode) {
        if (m_view->tool() == m_brush)
            (mode == BrushMode::Erase    ? m_eraserAct
             : mode == BrushMode::Smudge ? m_smudgeAct
             : mode == BrushMode::Clone  ? m_cloneAct
             : mode == BrushMode::Heal   ? m_healAct
                                         : m_brushAct)
                ->setChecked(true);
        updateCloneTool();
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
    createTransformOptions();
    createFillOptions();
    createGradientOptions();

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
    connect(m_layerPanel, &LayerPanel::maskEnabledChanged, this, &MainWindow::setLayerMaskEnabled);
    connect(m_layerPanel, &LayerPanel::addMaskRequested, this, &MainWindow::addLayerMask);
    connect(m_layerPanel, &LayerPanel::deleteMaskRequested, this, &MainWindow::deleteLayerMask);
    connect(m_layerPanel, &LayerPanel::applyMaskRequested, this, &MainWindow::applyLayerMask);
    connect(m_layerPanel, &LayerPanel::editMaskToggled, this, &MainWindow::setEditingMask);

    m_adjustPanel = new AdjustPanel;
    m_adjustDock = new QDockWidget(tr("Adjustment"), this);
    m_adjustDock->setObjectName(QStringLiteral("AdjustmentDock"));
    m_adjustDock->setWidget(m_adjustPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_adjustDock);
    connect(m_adjustPanel, &AdjustPanel::addRequested, this, &MainWindow::addAdjustmentLayer);
    connect(m_adjustPanel, &AdjustPanel::adjustmentChanged, this, [this](const easeletch::Adjustment &a) {
        if (m_stack)
            setAdjustment(m_stack->activeId(), a);
    });

    m_color = new ColorPanel;
    auto *colorDock = new QDockWidget(tr("Color"), this);
    colorDock->setObjectName(QStringLiteral("ColorDock"));
    colorDock->setWidget(m_color);
    addDockWidget(Qt::RightDockWidgetArea, colorDock);
    // Colour and Adjustment share a place: whichever suits the active layer
    // comes to the front.
    m_colorDock = colorDock;
    tabifyDockWidget(colorDock, m_adjustDock);
    colorDock->raise();
    connect(m_color, &ColorPanel::colorChanged, m_brush, &BrushTool::setColor);
    connect(m_color, &ColorPanel::colorChanged, this, &MainWindow::updateText);
    // The Text window shows the same colour, and its own button sets it.
    connect(m_color, &ColorPanel::colorChanged, m_textPanel, &TextPanel::setColor);
    connect(m_textPanel, &TextPanel::colorPicked, m_color, &ColorPanel::setColor);
    m_textPanel->setColor(m_color->color());
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

    connect(m_view, &CanvasView::viewChanged, this, &MainWindow::updateCloneTool);
    connect(m_view, &CanvasView::viewChanged, this, [this](double zoom, double rotation) {
        m_lasso->setCloseDistance(8.0 / qMax(zoom, 0.01)); // eight screen pixels
        if (m_xf.active && m_view->tool() == m_transformTool)
            updateTransformOutline(); // handles keep their size on screen
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

QAction *MainWindow::actionFor(CanvasTool *tool) const
{
    if (tool == m_brush)
        return m_brush->mode() == BrushMode::Erase    ? m_eraserAct
               : m_brush->mode() == BrushMode::Smudge ? m_smudgeAct
               : m_brush->mode() == BrushMode::Clone  ? m_cloneAct
               : m_brush->mode() == BrushMode::Heal   ? m_healAct
                                                      : m_brushAct;
    return tool == m_rectSelect      ? m_rectSelectAct
           : tool == m_ellipseSelect ? m_ellipseSelectAct
           : tool == m_move          ? m_moveAct
           : tool == m_wand          ? m_wandAct
           : tool == m_lasso         ? m_lassoAct
           : tool == m_textTool      ? m_textAct
           : tool == m_eyedropper    ? m_eyedropperAct
           : tool == m_transformTool ? m_transformAct
           : tool == m_fillTool      ? m_fillAct
           : tool == m_gradientTool  ? m_gradientAct
                                     : nullptr;
}

void MainWindow::activateTool(CanvasTool *tool, bool brushOptions)
{
    // Floating pixels are dropped where they are, unless the tool is one that
    // works on them: Move, Transform for a transform under way, or Text for
    // text being typed.
    const bool keepsFloating = m_xf.active ? tool == m_transformTool
                                           : (tool == m_move || (tool == m_textTool && m_text.active));
    if (!keepsFloating)
        commitFloating();
    if (tool != m_lasso)
        m_lasso->cancel();
    m_view->setTool(tool);
    m_options->setEnabled(brushOptions);
    // One options bar at a time. The old one is hidden before the new one is
    // shown: with two in the row, even for a moment, the window widens to
    // fit both and stays that wide.
    const std::pair<QToolBar *, bool> bars[] = {
        {m_options, tool != m_wand && tool != m_transformTool && tool != m_fillTool && tool != m_gradientTool},
        {m_wandOptions, tool == m_wand},
        {m_transformOptions, tool == m_transformTool},
        {m_fillOptions, tool == m_fillTool},
        {m_gradientOptions, tool == m_gradientTool},
    };
    for (const auto &[bar, on] : bars)
        if (!on)
            bar->hide();
    for (const auto &[bar, on] : bars)
        if (on)
            bar->show();
    if (QAction *act = actionFor(tool))
        act->setChecked(true);
    updateCloneTool();
}

void MainWindow::updateCloneTool()
{
    const bool cloning = m_view->tool() == m_brush && m_brush->mode() == BrushMode::Clone;
    m_view->setAltTool(cloning ? static_cast<CanvasTool *>(m_cloneSource) : m_eyedropper);
    if (m_xf.active) {
        m_cloneMarker = false; // the handles on the canvas are the transform's
        return;
    }
    if (cloning && m_brush->hasCloneSource()) {
        // A small diamond where the copy is taken from, the same size on
        // screen at any zoom.
        const QPointF c = m_brush->cloneSource();
        const double r = 6.0 / qMax(m_view->zoom(), 0.01);
        m_view->setHandles({QPolygonF({c + QPointF(0, -r), c + QPointF(r, 0), c + QPointF(0, r), c + QPointF(-r, 0)})});
        m_cloneMarker = true;
    } else if (m_cloneMarker) {
        m_view->setHandles({});
        m_cloneMarker = false;
    }
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
    // Delete stays on without a selection: then it deletes the layer.
    for (QAction *a : {m_cutAct, m_copyAct, m_deselectAct, m_cropAct, m_exportSelectionAct,
                       m_growAct, m_shrinkAct, m_featherAct})
        if (a)
            a->setEnabled(any);
    if (m_selectionLabel) {
        const QRect b = m_selection.bounds();
        m_selectionLabel->setText(any ? tr(m_selection.isSoft() ? "Feathered selection %1 × %2 at %3, %4"
                                                               : "Selection %1 × %2 at %3, %4")
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
    if (!m_stack || m_view->isStroking())
        return;
    // With something selected, Delete clears those pixels. With nothing
    // selected it means the highlighted layer, wherever the keyboard focus is.
    if (m_selection.isEmpty() && !m_floating.isActive() && !m_text.active) {
        deleteLayer();
        return;
    }
    if (m_selection.isEmpty())
        return;
    clearSelected(tr("Delete"));
}

void MainWindow::clearSelected(const QString &label)
{
    const easeletch::Selection target = m_selection;
    QHash<easeletch::TileCoord, QImage> before;
    int layerId = m_stack->activeId();
    bool onMask = m_editMask;
    if (m_floating.isActive()) {
        // Floating pixels just go away: for a paste that's no change at all,
        // for a lift it's the original area cleared.
        const bool lifted = m_floating.wasLifted();
        const easeletch::Selection origin = m_selectionBeforeFloat;
        m_floating.cancel();
        endTransform();
        layerId = m_floatLayer;
        onMask = m_floatMask;
        easeletch::Layer *l = m_stack->layer(layerId);
        if (lifted && l)
            before = easeletch::clearSelection(onMask ? l->mask : l->store, origin, canvasRect());
    } else {
        easeletch::TileStore *store = editStore();
        if (!store)
            return;
        before = easeletch::clearSelection(*store, target, canvasRect());
    }
    if (!before.isEmpty())
        m_history.push(label, layerId, std::move(before), onMask);
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
    m_floatMask = m_editMask;
    m_floating.paste(store, content, pos, shape, canvasRect());
    m_floatLabel = tr("Paste");
    m_floatPasted = true;
    m_floatStart = QPoint(INT_MIN, INT_MIN); // always a change
    setSelection(m_floating.selection());
    activateTool(m_move, false);
    m_view->refresh();
}

bool MainWindow::liftForMove()
{
    if (m_floating.isActive())
        return true;
    if (m_text.active)
        return false; // nothing typed yet: there's nothing to move
    easeletch::TileStore *store = m_stack ? editStore() : nullptr;
    if (!store)
        return false;
    // With nothing selected, Move takes the whole layer: the part of it that
    // has anything in it, so a small piece of text on a big canvas doesn't
    // turn into a canvas-sized block of empty tiles.
    easeletch::Selection sel = m_selection;
    if (sel.isEmpty()) {
        const QRect content = easeletch::opaqueBounds(*store, canvasRect());
        if (content.isEmpty()) {
            statusBar()->showMessage(tr("There's nothing on this layer to move"), 4000);
            return false;
        }
        sel = easeletch::Selection::rect(content);
    }
    m_selectionBeforeFloat = m_selection;
    m_floatLayer = m_stack->activeId();
    m_floatMask = m_editMask;
    // A whole text layer on the move stays text: it's drawn again where it lands.
    const easeletch::Layer *active = m_stack->active();
    m_floatText = m_selection.isEmpty() && !m_editMask && active && active->isText();
    m_floatTextBefore = m_floatText ? m_stack->snapshot() : easeletch::LayerStack();
    m_floating.lift(store, sel, canvasRect());
    if (!m_floating.isActive()) {
        m_floatText = false;
        m_floatTextBefore = easeletch::LayerStack();
        return false;
    }
    m_floatLabel = tr("Move");
    m_floatPasted = false;
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
    showFloatingOutline();
    m_view->refresh();
}

void MainWindow::nudge(const QPoint &delta)
{
    if (m_moveDragging || !liftForMove())
        return;
    m_floating.moveBy(delta);
    showFloatingOutline();
    m_view->refresh();
}

void MainWindow::showFloatingOutline()
{
    // Text being typed isn't a selection: it only gets a frame.
    if (m_text.active)
        m_view->setSelectionOutline(
            easeletch::Selection::rect(QRect(m_floating.position(), m_floating.content().size())).outlines());
    else
        setSelection(m_floating.selection());
}

void MainWindow::commitFloating()
{
    if (m_text.active) {
        commitText();
        return;
    }
    if (!m_floating.isActive())
        return;
    if (m_floating.position() == m_floatStart) {
        // Lifted and put back where it was: nothing to record.
        cancelFloating();
        return;
    }
    if (m_xf.active && m_xf.dragging) {
        m_xf.dragging = false;
        applyTransform(true); // a drag may have left a quick preview
    }
    if (m_floatText && !m_floatPasted && !m_xf.active) {
        // Moved, not changed: the text is drawn again from its words at the
        // new place (so nothing is lost if part of it was off the canvas),
        // and the step recorded is the layer as a whole, words and all.
        const QPoint delta = m_floating.position() - m_floatStart;
        m_floating.commit();
        if (easeletch::Layer *l = m_stack->layer(m_floatLayer)) {
            l->textAnchor += delta;
            easeletch::drawTextLayer(*l, canvasRect());
        }
        m_history.pushState(m_floatLabel, std::move(m_floatTextBefore), *m_stack);
        m_floatText = false;
        m_floatTextBefore = easeletch::LayerStack();
        m_moveDragging = false;
        setSelection({});
        layersChanged();
        return;
    }
    m_floatText = false;
    m_floatTextBefore = easeletch::LayerStack();
    // The outline around floating pixels is only there to show what's being
    // placed. Once they're down it stays only if the user had selected that
    // area themselves: a paste, or a move or transform of the whole layer,
    // leaves nothing selected, so the next brush stroke paints anywhere.
    const bool keepSelection = !m_floatPasted && !m_selectionBeforeFloat.isEmpty()
                               && (!m_xf.active || m_xf.hadSelection);
    const easeletch::Selection placed = m_floating.selection();
    m_history.push(m_floatLabel, m_floatLayer, m_floating.commit(), m_floatMask);
    m_moveDragging = false;
    endTransform();
    setSelection(keepSelection ? placed : easeletch::Selection());
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
    for (const easeletch::Layer &l : m_stack->layers()) {
        easeletch::Layer *layer = m_stack->layer(l.id);
        if (l.isText()) {
            // Text stays text: it's drawn again at its place on the new canvas.
            layer->textAnchor -= rect.topLeft();
            easeletch::drawTextLayer(*layer, QRect(QPoint(0, 0), rect.size()));
        } else if (!l.group) {
            easeletch::cropStore(layer->store, rect);
        }
        if (l.hasMask)
            easeletch::cropStore(layer->mask, rect);
    }
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

// --- Text -------------------------------------------------------------------------

namespace {

// What a text layer is called: what it says, cut short.
QString textLayerName(const QString &text)
{
    QString name = text.simplified();
    if (name.size() > 24)
        name = name.left(24) + QStringLiteral("…");
    return name;
}

} // namespace

void MainWindow::beginText(const QPoint &pos)
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating(); // places text already in progress, or anything else floating
    m_text.before = m_stack->snapshot();

    // Text gets a layer of its own, above the active one.
    easeletch::Layer l;
    l.name = m_stack->uniqueName(tr("Text"));
    const easeletch::Layer *active = m_stack->active();
    int id = 0;
    if (active && active->group) {
        id = m_stack->insert(std::move(l), active->id, INT_MAX);
    } else {
        const int parent = active ? active->parent : 0;
        const int at = active ? int(m_stack->children(parent).indexOf(active->id)) + 1 : INT_MAX;
        id = m_stack->insert(std::move(l), parent, at);
    }
    m_stack->setActive(id);
    m_editMask = false;
    m_text.active = true;
    m_text.editing = false;
    m_text.nameBefore.clear();
    m_text.layerId = id;
    m_text.anchor = pos;
    m_text.placed = pos;
    m_text.box = QRect();
    m_text.dragging = false;
    layersChanged();

    m_textPanel->setEditing(false);
    showTextPanel(pos);
    updateText();
}

int MainWindow::textLayerAt(const QPoint &pos) const
{
    if (!m_stack)
        return 0;
    const QList<easeletch::Layer> &layers = m_stack->layers();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it)
        if (it->textBox.contains(pos) && m_stack->isShown(it->id) && it->isText())
            return it->id;
    return 0;
}

bool MainWindow::editText(int layerId)
{
    if (!m_stack || m_view->isStroking())
        return false;
    commitFloating();
    easeletch::Layer *l = m_stack->layer(layerId);
    if (!l || !l->isText())
        return false;
    if (m_stack->isLocked(layerId)) {
        statusBar()->showMessage(tr("\"%1\" is locked").arg(l->name), 4000);
        return false;
    }
    const easeletch::TextSettings settings = l->text;
    const QPoint anchor = l->textAnchor;
    // The panels show the text as it is: its words and font, and its colour.
    m_color->setColor(settings.color);
    {
        const QSignalBlocker block(m_textPanel);
        m_textPanel->setSettings(settings);
    }
    m_text.before = m_stack->snapshot();
    // The words float again, over an empty layer, exactly as while first typed.
    l->store.clear();
    m_stack->setActive(layerId);
    m_editMask = false;
    m_text.active = true;
    m_text.editing = true;
    m_text.nameBefore = textLayerName(settings.text);
    m_text.layerId = layerId;
    m_text.anchor = anchor;
    m_text.placed = anchor;
    m_text.box = QRect();
    m_text.dragging = false;
    layersChanged();

    m_textPanel->setEditing(true);
    showTextPanel(anchor);
    updateText();
    return true;
}

void MainWindow::showTextPanel(const QPoint &pos)
{
    if (!m_textPanel->isVisible()) {
        // Out of the way of the canvas: in a lower corner of it, the one
        // further from where the text is going. After that it stays wherever
        // it's dragged to.
        if (m_textPanelPos.isNull()) {
            m_textPanel->adjustSize();
            const QRect view(m_view->mapToGlobal(QPoint(0, 0)), m_view->size());
            const QPointF click = m_view->canvasToView().map(QPointF(pos));
            const bool clickOnRight = click.x() > view.width() / 2.0;
            const int x = clickOnRight ? view.left() + 16 : view.right() - m_textPanel->width() - 16;
            m_textPanelPos = QPoint(x, view.bottom() - m_textPanel->height() - 48);
        }
        m_textPanel->move(m_textPanelPos);
    }
    m_textPanel->show();
    m_textPanel->raise();
    m_textPanel->activateWindow();
    m_textPanel->focusText();
}

void MainWindow::updateText()
{
    if (!m_text.active || !m_stack)
        return;
    easeletch::Layer *l = m_stack->layer(m_text.layerId);
    if (!l)
        return;
    // The text may have been dragged since it was last drawn.
    if (m_floating.isActive()) {
        m_text.anchor += m_floating.position() - m_text.placed;
        m_floating.cancel();
    }
    easeletch::TextSettings s = m_textPanel->settings();
    s.color = m_color->color();
    const easeletch::TextLayout layout = easeletch::layoutText(s, m_text.anchor);
    if (layout.image.isNull()) {
        m_text.hasTail = false;
        showTailHandle();
        m_view->setSelectionOutline(m_selection.outlines());
        m_view->refresh();
        return;
    }
    m_text.placed = layout.origin;
    m_text.box = layout.box;
    m_text.centre = layout.centre;
    m_text.hasTail = layout.hasTail;
    m_text.tailTip = layout.tailTip;
    m_floating.paste(&l->store, layout.image, m_text.placed, {}, canvasRect());
    m_floatLayer = l->id;
    m_floatMask = false;
    showFloatingOutline();
    showTailHandle();
    m_view->refresh();
}

void MainWindow::showTailHandle()
{
    if (!m_text.active || !m_text.hasTail || !m_floating.isActive()) {
        if (!m_xf.active)
            m_view->setHandles({});
        return;
    }
    // Twelve screen pixels across, whatever the zoom, where the tail points now.
    const QPointF tip = QPointF(m_text.tailTip + m_floating.position() - m_text.placed) + QPointF(0.5, 0.5);
    const double half = 6.0 / qMax(m_view->zoom(), 0.01);
    QPolygonF diamond;
    diamond << tip + QPointF(0, -half) << tip + QPointF(half, 0) << tip + QPointF(0, half) << tip + QPointF(-half, 0);
    m_view->setHandles({diamond});
}

void MainWindow::textPressed(const QPointF &pos)
{
    // A click starts new text, but only once the click is over: the canvas is
    // still in the middle of it here. A drag instead moves what's on the
    // layer, so placed text can be shifted without changing tools.
    m_text.clickStarts = !m_text.active;
    m_dragStart = pos;
    if (!m_text.active)
        return;
    // On the tail's handle, the drag aims the tail; anywhere else it moves the text.
    if (m_text.hasTail && m_floating.isActive()) {
        const QPointF tip = QPointF(m_text.tailTip + m_floating.position() - m_text.placed) + QPointF(0.5, 0.5);
        if (QLineF(pos, tip).length() * m_view->zoom() <= 10.0) {
            m_text.draggingTail = true;
            return;
        }
    }
    m_text.dragging = m_floating.isActive();
    m_dragStart = pos;
    m_dragOrigin = m_floating.position();
}

void MainWindow::textDragged(const QPointF &pos)
{
    if (!m_text.active) {
        if (m_text.clickStarts) {
            // Four screen pixels of travel make it a drag, not a click.
            if (QLineF(pos, m_dragStart).length() * m_view->zoom() < 4.0)
                return;
            m_text.clickStarts = false;
            m_text.movingLayer = true;
            m_text.selectionBeforeMove = m_selection;
            moveDragStarted(m_dragStart);
        }
        if (m_text.movingLayer)
            moveDragged(pos);
        return;
    }
    if (m_text.draggingTail) {
        // From the middle of the words as they sit now (they may have been dragged).
        const QPoint centre = m_text.centre + m_floating.position() - m_text.placed;
        m_textPanel->setTailOffset(QPoint(int(std::floor(pos.x())), int(std::floor(pos.y()))) - centre);
        return;
    }
    if (!m_text.dragging || !m_floating.isActive())
        return;
    const QPointF d = pos - m_dragStart;
    m_floating.moveTo(m_dragOrigin + QPoint(int(std::lround(d.x())), int(std::lround(d.y()))));
    showFloatingOutline();
    showTailHandle();
    m_view->refresh();
}

void MainWindow::textReleased(const QPointF &pos)
{
    m_text.dragging = false;
    if (m_text.draggingTail) {
        m_text.draggingTail = false;
        return;
    }
    if (m_text.movingLayer) {
        // The drag moved the layer: drop it where it is, as one undo step.
        m_text.movingLayer = false;
        moveDragged(pos);
        m_moveDragging = false;
        const bool hadSelection = !m_text.selectionBeforeMove.isEmpty();
        commitFloating();
        if (!hadSelection)
            setSelection({}); // it was the whole layer, not a selection
        return;
    }
    if (!m_text.clickStarts || m_text.active)
        return;
    m_text.clickStarts = false;
    const QPoint at(int(std::floor(pos.x())), int(std::floor(pos.y())));
    QTimer::singleShot(0, this, [this, at] {
        if (m_text.active || m_view->tool() != m_textTool)
            return;
        // On words already placed, the click changes them; anywhere else it
        // starts new ones.
        if (const int id = textLayerAt(at))
            editText(id);
        else
            beginText(at);
    });
}

void MainWindow::commitText()
{
    if (!m_text.active)
        return;
    if (!m_floating.isActive()) {
        cancelText(); // nothing was typed (or, changing text, all of it was deleted)
        return;
    }
    // It may have been dragged since it was last drawn.
    const QPoint moved = m_floating.position() - m_text.placed;
    m_floating.commit(); // the pixels stay; the step recorded is the document before
    const bool editing = m_text.editing;
    m_text.active = false;
    m_text.dragging = false;
    m_moveDragging = false;
    if (easeletch::Layer *l = m_stack->layer(m_text.layerId)) {
        easeletch::TextSettings s = m_textPanel->settings();
        s.color = m_color->color();
        // Named after what it says, unless it's been given a name of its own.
        const QString name = textLayerName(s.text);
        if (!name.isEmpty() && (!editing || l->name == m_text.nameBefore))
            l->name = name;
        // The layer keeps its words, so they can be changed later.
        l->hasText = true;
        l->text = s;
        l->textAnchor = m_text.anchor + moved;
        l->textBox = m_text.box.translated(moved);
        l->textPixels = l->store.snapshot();
    }
    m_history.pushState(editing ? tr("Edit Text") : tr("Text"), std::move(m_text.before), *m_stack);
    m_text.before = easeletch::LayerStack();
    m_text.editing = false;
    m_view->setSelectionOutline(m_selection.outlines());
    hideTextPanel();
    layersChanged();
}

void MainWindow::hideTextPanel()
{
    if (m_textPanel->isVisible())
        m_textPanelPos = m_textPanel->pos();
    const bool wasTyping = m_textPanel->isVisible();
    const QSignalBlocker block(m_textPanel);
    m_textPanel->setText(QString());
    m_textPanel->setBoxWidth(0); // wrapping belongs to one piece of text, not the next
    m_textPanel->setTailOffset(QPoint()); // ... and so does where its tail was aimed
    if (!m_xf.active)
        m_view->setHandles({});
    m_textPanel->hide();
    // The keyboard goes back to the canvas, so the next key (Delete, Ctrl+Z,
    // a tool letter) acts on the picture and not on a window that's gone.
    if (wasTyping) {
        activateWindow();
        m_view->setFocus();
    }
}

void MainWindow::cancelText()
{
    if (!m_text.active)
        return;
    m_floating.cancel();
    m_text.active = false;
    m_text.dragging = false;
    m_moveDragging = false;
    m_text.editing = false;
    // Back to the document as it was before the text layer was made, or
    // before its words were opened again.
    m_stack->swapState(m_text.before);
    m_text.before = easeletch::LayerStack();
    m_view->setSelectionOutline(m_selection.outlines());
    hideTextPanel();
    layersChanged();
}

void MainWindow::featherSelection(int pixels)
{
    if (m_selection.isEmpty() || pixels <= 0)
        return;
    commitFloating();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const easeletch::Selection s = m_selection.feathered(pixels, canvasRect());
    QApplication::restoreOverrideCursor();
    setSelection(s);
}

void MainWindow::lassoFinished(const QPolygonF &path, Qt::KeyboardModifiers modifiers)
{
    if (!m_stack)
        return;
    commitFloating();
    const easeletch::Selection drawn = easeletch::Selection::polygon(path, canvasRect());
    if (modifiers & Qt::ShiftModifier)
        setSelection(m_selection.united(drawn));
    else if (modifiers & Qt::ControlModifier)
        setSelection(m_selection.subtracted(drawn));
    else
        setSelection(drawn);
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
    m_history.push(tr("Color to Alpha"), m_stack->activeId(), std::move(before), m_editMask);
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
    if (m_text.active) {
        cancelText();
        return;
    }
    if (!m_floating.isActive())
        return;
    m_floating.cancel();
    m_floatText = false;
    m_floatTextBefore = easeletch::LayerStack();
    m_moveDragging = false;
    endTransform();
    setSelection(m_selectionBeforeFloat);
    m_view->refresh();
}

// --- Layers ---------------------------------------------------------------------

easeletch::TileStore *MainWindow::layer() const
{
    easeletch::Layer *l = m_stack ? m_stack->active() : nullptr;
    return l && l->hasPixels() ? &l->store : nullptr;
}

easeletch::TileStore *MainWindow::editStore()
{
    easeletch::Layer *l = m_stack ? m_stack->active() : nullptr;
    if (!l)
        return nullptr;
    const bool onMask = m_editMask && l->hasMask;
    QString why;
    if (l->group && !onMask)
        why = tr("A group has no pixels of its own: pick a layer inside it");
    else if (l->isAdjustment() && !onMask)
        why = tr("An adjustment layer has no pixels: change it in the Adjustment panel, "
                 "or add a mask to it and paint on that");
    else if (m_stack->isLocked(l->id))
        why = tr("\"%1\" is locked").arg(l->name);
    else if (!m_stack->isShown(l->id))
        why = tr("\"%1\" is hidden").arg(l->name);
    if (!why.isEmpty()) {
        statusBar()->showMessage(why, 4000);
        return nullptr;
    }
    return onMask ? &l->mask : &l->store;
}

const easeletch::TileStore *MainWindow::readStore() const
{
    easeletch::Layer *l = m_stack->active();
    if (l && m_editMask && l->hasMask)
        return &l->mask;
    if (const easeletch::TileStore *store = layer())
        return store;
    syncComposite();
    return &m_stack->composite();
}

void MainWindow::bindTools()
{
    const QRect bounds = canvasRect();
    easeletch::Layer *l = m_stack->active();
    m_editMask = m_editMask && l && l->hasMask;
    const bool paintable = l && (m_editMask || l->hasPixels()) && !m_stack->isLocked(l->id) && m_stack->isShown(l->id);
    m_brush->setDocument(paintable ? (m_editMask ? &l->mask : &l->store) : nullptr, bounds, &m_history,
                         l ? l->id : 0, m_editMask);
    m_layerPanel->setEditingMask(m_editMask);
    if (m_editMaskAct) {
        m_editMaskAct->setEnabled(l && l->hasMask);
        m_editMaskAct->setChecked(m_editMask);
    }
    m_adjustPanel->setAdjustment(l ? l->adjust : easeletch::Adjustment());
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
    m_editMask = false; // a newly picked layer is painted on, not its mask
    bindTools();
    m_layerPanel->syncActive();
    showPanelForActiveLayer();
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

bool MainWindow::addAdjustmentLayer(easeletch::AdjustmentType type)
{
    if (type == easeletch::AdjustmentType::None || !beginLayerChange())
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    easeletch::Layer l;
    l.adjust = easeletch::Adjustment::make(type);
    l.name = m_stack->uniqueName(AdjustPanel::typeName(type));
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
    m_editMask = false;
    finishLayerChange(tr("New %1 Layer").arg(AdjustPanel::typeName(type)), std::move(before));
    m_adjustDock->show();
    showPanelForActiveLayer();
    return true;
}

void MainWindow::showPanelForActiveLayer()
{
    // Only while the two are tabbed together: pulled apart, both are in view.
    if (!m_stack || !tabifiedDockWidgets(m_colorDock).contains(m_adjustDock))
        return;
    const easeletch::Layer *l = m_stack->active();
    (l && l->isAdjustment() ? m_adjustDock : m_colorDock)->raise();
}

void MainWindow::setAdjustment(int id, const easeletch::Adjustment &adjustment)
{
    if (!m_stack || m_view->isStroking())
        return;
    const easeletch::Layer *l = m_stack->layer(id);
    const easeletch::Adjustment next = adjustment.normalized();
    if (!l || !l->isAdjustment() || next.type != l->adjust.type || l->adjust == next)
        return;
    commitFloating();
    if (m_stack->isLocked(id)) {
        statusBar()->showMessage(tr("\"%1\" is locked").arg(l->name), 4000);
        m_adjustPanel->setAdjustment(l->adjust); // the controls go back
        return;
    }
    // Dragging a slider or a curve point sends many values; they add up to
    // one undo step.
    const bool continuing = m_adjustLayer == id && m_adjustState == m_history.stateId();
    easeletch::LayerStack before;
    if (!continuing)
        before = m_stack->snapshot();
    m_stack->layer(id)->adjust = next;
    if (!continuing) {
        m_history.pushState(tr("Change %1").arg(AdjustPanel::typeName(next.type)), std::move(before), *m_stack);
        m_adjustLayer = id;
        m_adjustState = m_history.stateId();
        historyChanged();
    }
    // Redraw once the queued moves are in, not once per move.
    if (!m_opacityRedrawPending) {
        m_opacityRedrawPending = true;
        QTimer::singleShot(0, this, &MainWindow::flushOpacityRedraw);
    }
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
    const QString name = m_stack->active()->name;
    m_stack->remove(id);
    statusBar()->showMessage(tr("Deleted layer \"%1\". Ctrl+Z brings it back.").arg(name), 5000);
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

bool MainWindow::addLayerMask()
{
    if (!beginLayerChange() || !m_stack->active() || m_stack->active()->hasMask)
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    const int id = m_stack->activeId();
    m_stack->addMask(id);
    if (!m_selection.isEmpty()) {
        // Show only what's selected: black everywhere, the selection in white
        // (grey where it's feathered).
        easeletch::Layer *l = m_stack->layer(id);
        l->mask = easeletch::TileStore(Qt::black);
        const QRect area = m_selection.bounds() & canvasRect();
        constexpr int N = easeletch::TileStore::TileSize;
        for (const easeletch::TileCoord c : easeletch::TileStore::tilesIntersecting(area)) {
            const QRect tr = easeletch::TileStore::tileRect(c);
            const QRect part = tr & area;
            auto *d = reinterpret_cast<easeletch::Pixel *>(l->mask.writableTile(c).bits());
            for (int y = part.top(); y <= part.bottom(); ++y) {
                for (int x = part.left(); x <= part.right(); ++x) {
                    const float k = m_selection.coverage(x, y);
                    d[(y - tr.top()) * N + (x - tr.left())] = easeletch::makePixel(k, k, k, 1.0f);
                }
            }
        }
    }
    m_editMask = true;
    finishLayerChange(tr("Add Layer Mask"), std::move(before));
    statusBar()->showMessage(tr("Painting on the mask: black hides, white shows. Ctrl+M goes back to the layer."),
                             6000);
    return true;
}

bool MainWindow::deleteLayerMask()
{
    if (!beginLayerChange() || !m_stack->active() || !m_stack->active()->hasMask)
        return false;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->removeMask(m_stack->activeId());
    finishLayerChange(tr("Remove Layer Mask"), std::move(before));
    return true;
}

bool MainWindow::applyLayerMask()
{
    if (!beginLayerChange() || !m_stack->active() || !m_stack->active()->hasMask)
        return false;
    if (m_stack->active()->group) {
        statusBar()->showMessage(tr("A group's mask can't be applied: merge the group first"), 4000);
        return false;
    }
    if (m_stack->active()->isAdjustment()) {
        statusBar()->showMessage(tr("An adjustment layer's mask can't be applied: there are no pixels to erase"), 4000);
        return false;
    }
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->applyMask(m_stack->activeId());
    finishLayerChange(tr("Apply Layer Mask"), std::move(before));
    return true;
}

void MainWindow::setLayerMaskEnabled(int id, bool enabled)
{
    if (!beginLayerChange() || !m_stack->layer(id) || !m_stack->layer(id)->hasMask
        || m_stack->layer(id)->maskEnabled == enabled)
        return;
    easeletch::LayerStack before = m_stack->snapshot();
    m_stack->layer(id)->maskEnabled = enabled;
    finishLayerChange(enabled ? tr("Enable Layer Mask") : tr("Disable Layer Mask"), std::move(before));
}

void MainWindow::setEditingMask(bool on)
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating();
    const easeletch::Layer *l = m_stack->active();
    m_editMask = on && l && l->hasMask;
    bindTools();
    if (m_editMask)
        statusBar()->showMessage(tr("Painting on the mask: black hides, white shows"), 4000);
    else if (on)
        statusBar()->showMessage(tr("This layer has no mask: add one first"), 4000);
    else
        statusBar()->clearMessage();
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
                std::move(doc.pyramid), doc.native ? info.absoluteFilePath() : QString(), std::move(doc.pages),
                doc.activePage);
    emit documentOpened(path, true);
}

bool MainWindow::isModified() const
{
    if (m_history.stateId() != m_cleanId || m_pagesRevision != m_cleanRevision)
        return true;
    for (int i = 0; i < pageCount(); ++i)
        if (i != m_page && m_pages[i].history.stateId() != m_pages[i].cleanId)
            return true;
    return false;
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
    m_adjustLayer = 0;
    // Every page goes into the file; the others haven't changed since they
    // were last worked on.
    QList<easeletch::DocumentPage> pages;
    SavedState state;
    state.docGeneration = m_docGeneration;
    state.pagesRevision = m_pagesRevision;
    for (int i = 0; i < pageCount(); ++i) {
        const bool current = i == m_page;
        pages.append({m_pages[i].name, current ? *m_stack : *m_pages[i].stack});
        state.pages.append({m_pages[i].id, current ? m_history.stateId() : m_pages[i].history.stateId()});
    }
    const int activePage = m_page;
    m_lastDir = QFileInfo(path).absolutePath();

    if (wait) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        const QString error = easeletch::saveNativeDocument(path, std::move(pages), activePage);
        QApplication::restoreOverrideCursor();
        ++m_pendingJobs; // balanced in finishSave
        finishSave(path, state, error);
        return error.isEmpty();
    }

    statusBar()->showMessage(tr("Saving %1…").arg(QFileInfo(path).fileName()));
    ++m_pendingJobs;
    QPointer<MainWindow> self(this);
    auto shared = std::make_shared<QList<easeletch::DocumentPage>>(std::move(pages));
    QThreadPool::globalInstance()->start([self, path, shared, activePage, state] {
        const QString error = easeletch::saveNativeDocument(path, std::move(*shared), activePage);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, state, error] {
                if (self)
                    self->finishSave(path, state, error);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void MainWindow::finishSave(const QString &path, const SavedState &state, const QString &error)
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
    if (state.docGeneration == m_docGeneration) {
        m_path = QFileInfo(path).absoluteFilePath();
        m_name = name;
        // Later strokes, and pages added or changed since, still count as unsaved.
        m_cleanRevision = state.pagesRevision;
        for (const auto &[id, stateId] : state.pages) {
            for (int i = 0; i < pageCount(); ++i) {
                if (m_pages[i].id != id)
                    continue;
                if (i == m_page)
                    m_cleanId = stateId;
                else
                    m_pages[i].cleanId = stateId;
            }
        }
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
                             const QString &historyLabel, easeletch::TilePyramid pyramid, const QString &path,
                             std::vector<easeletch::LoadedPage> pages, int activePage)
{
    // Detach everything from the old document before it's freed.
    m_filterPreview = FilterPreview();
    m_floating.cancel();
    endTransform();
    m_lasso->cancel();
    if (m_text.active) {
        // The document it belonged to is going away.
        m_text = TextSession();
        hideTextPanel();
    }
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
    m_adjustLayer = 0;
    m_editMask = false;

    // The pages: the one given, or those of an opened document with the
    // given one at activePage.
    m_pages.clear();
    m_nextPageId = 1;
    m_pagesRevision = m_cleanRevision = 0;
    if (pages.empty())
        pages.emplace_back();
    m_page = std::clamp(activePage, 0, int(pages.size()) - 1);
    for (int i = 0; i < int(pages.size()); ++i) {
        Page page;
        page.id = m_nextPageId++;
        page.name = pages[i].name.isEmpty() ? tr("Page %1").arg(i + 1) : pages[i].name;
        if (i != m_page) {
            page.stack = std::move(pages[i].stack);
            page.history.reset(historyLabel);
            page.cleanId = page.history.stateId();
        }
        m_pages.push_back(std::move(page));
    }

    m_view->setDocument(m_stack->compositeStore(), canvasSize(), std::move(pyramid));
    bindTools();
    m_layerPanel->setStack(m_stack.get());
    syncPageTabs();

    historyChanged();
    updateTitle();
    updateMemoryLabel();
}

void MainWindow::undo()
{
    if (!m_stack || m_view->isStroking())
        return;
    if (m_floating.isActive() || m_text.active) {
        // Undoing an uncommitted move, paste or text is just putting it back.
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
    if (!m_stack || m_view->isStroking() || m_floating.isActive() || m_text.active || !m_history.canRedo())
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
    // With more than one page, the title says which one this is.
    const QString page = pageCount() > 1 ? tr(" · %1").arg(pageName(m_page)) : QString();
    setWindowTitle(tr("%1[*]%2 (%3 × %4) — Easeletch")
                       .arg(m_name, page)
                       .arg(canvasSize().width())
                       .arg(canvasSize().height()));
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
    // The current page is what's exported; with several, its name goes in the file's.
    const QString base = QFileInfo(m_name).completeBaseName()
                         + (pageCount() > 1 ? QLatin1Char('-') + pageName(m_page) : QString());
    const QString suggested = QDir(m_lastDir).filePath(base + QStringLiteral(".png"));
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
    m_textPanel->saveSettings(settings);
    m_grid.save(settings);
    settings.setValue(QStringLiteral("transform/smooth"), m_transformSmooth);
    settings.setValue(QStringLiteral("fill/tolerance"), m_fill.tolerance);
    settings.setValue(QStringLiteral("fill/contiguous"), m_fill.contiguous);
    settings.setValue(QStringLiteral("fill/allLayers"), m_fill.allLayers);
    settings.setValue(QStringLiteral("gradient/shape"), easeletch::gradientShapeKey(m_gradient.shape));
    settings.setValue(QStringLiteral("gradient/preset"), m_gradient.preset);
    settings.setValue(QStringLiteral("gradient/custom"), easeletch::stopsToString(m_gradient.custom));
    QStringList saved;
    for (const easeletch::GradientPreset &g : std::as_const(m_userGradients))
        saved << g.name + QLatin1Char('\t') + easeletch::stopsToString(g.stops);
    settings.setValue(QStringLiteral("gradient/saved"), saved);
    settings.setValue(QStringLiteral("gradient/reverse"), m_gradient.reverse);
    settings.setValue(QStringLiteral("gradient/end"), m_gradient.end.name());
    settings.setValue(QStringLiteral("wand/tolerance"), m_wand->tolerance());
    settings.setValue(QStringLiteral("wand/contiguous"), m_wand->contiguous());
    event->accept();
}
