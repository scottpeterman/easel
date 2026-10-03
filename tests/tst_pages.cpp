#include "adjustpanel.h"
#include "brushtool.h"
#include "canvasview.h"
#include "documentio.h"
#include "layerpanel.h"
#include "mainwindow.h"
#include "selecttools.h"

#include <QSignalSpy>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>

using namespace easeletch;

namespace {

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

// What the document looks like at a canvas pixel of the current page.
QColor shown(MainWindow &w, int x, int y)
{
    w.canvasView()->refresh();
    return at(w.layers().composite(), x, y);
}

// What the GPU actually drew there.
QColor drawn(MainWindow &w, QPointF canvas)
{
    CanvasView *view = w.canvasView();
    view->refresh();
    QImage shot;
    for (int i = 0; i < 50; ++i) {
        shot = view->grabFramebuffer();
        if (shot.isNull() || view->lastFrameStats().fallbacks == 0)
            break;
    }
    const qreal dpr = qreal(shot.width()) / qreal(view->width());
    return shot.pixelColor((view->canvasToView().map(canvas) * dpr).toPoint());
}

bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    BrushSettings b = w.brushTool()->settings();
    b.size = 20;
    b.hardness = 1.0;
    b.opacity = 1.0;
    b.flow = 1.0;
    b.stabilizer = 0.0;
    b.pixel = false;
    b.pressureOpacity = false;
    w.brushTool()->setMode(BrushMode::Paint);
    w.brushTool()->setSettings(b);
    w.brushTool()->setColor(Qt::red);
}

void stroke(MainWindow &w, QPointF from, QPointF to)
{
    CanvasView *view = w.canvasView();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, from));
    for (int i = 1; i <= 10; ++i)
        QTest::mouseMove(view, viewPos(w, from + (to - from) * (i / 10.0)));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, to));
}

} // namespace

class TestPages : public QObject
{
    Q_OBJECT

private slots:
    void newDocumentHasOnePage()
    {
        MainWindow w;
        setupWindow(w);
        QCOMPARE(w.pageCount(), 1);
        QCOMPARE(w.currentPage(), 0);
        QCOMPARE(w.pageName(0), QStringLiteral("Page 1"));
        QCOMPARE(w.pageTabs()->count(), 1);
        QVERIFY(!w.isModified());
        QVERIFY(!w.deletePage(0)); // the last one stays
        QCOMPARE(w.pageCount(), 1);
    }

    void pagesKeepTheirOwnPicture()
    {
        MainWindow w;
        setupWindow(w);
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));

        // A new page has its own size and starts empty.
        QVERIFY(w.addPage(QSize(200, 600), Qt::white));
        QCOMPARE(w.pageCount(), 2);
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(w.pageName(1), QStringLiteral("Page 2"));
        QCOMPARE(w.canvasSize(), QSize(200, 600));
        QCOMPARE(w.canvasView()->canvasSize(), QSize(200, 600));
        QCOMPARE(w.pageSize(0), QSize(400, 300));
        QCOMPARE(w.pageTabs()->count(), 2);
        QCOMPARE(w.pageTabs()->currentIndex(), 1);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(w.windowTitle().contains(QStringLiteral("Page 2")));
        QVERIFY(w.windowTitle().contains(QStringLiteral("200 × 600")));

        w.canvasView()->setZoomCentered(1.0);
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {100, 50}, {100, 500});
        QCOMPARE(shown(w, 100, 300), QColor(Qt::blue));

        // Back on the first page everything is as it was left, on screen too.
        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QCOMPARE(shown(w, 100, 250), QColor(Qt::white));
        QVERIFY(near(drawn(w, {150, 100}), Qt::red));
        QVERIFY(near(drawn(w, {100, 250}), Qt::white));
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 1);

        // ... and the brush paints on the page that's showing.
        w.brushTool()->setColor(Qt::green);
        stroke(w, {50, 200}, {300, 200});
        QCOMPARE(shown(w, 150, 200), QColor(Qt::green));
        QVERIFY(w.setCurrentPage(1));
        QCOMPARE(shown(w, 150, 200), QColor(Qt::white));
        QCOMPARE(shown(w, 100, 300), QColor(Qt::blue));
        QVERIFY(near(drawn(w, {100, 300}), Qt::blue));
    }

    void eachPageHasItsOwnHistoryLayersAndSelection()
    {
        MainWindow w;
        setupWindow(w);
        stroke(w, {50, 100}, {300, 100});
        QVERIFY(w.addLayer());
        w.setSelection(Selection::rect(QRect(10, 10, 50, 40)));
        QCOMPARE(w.history().position(), 2);

        QVERIFY(w.addPage(QSize(400, 300), Qt::white));
        QCOMPARE(w.history().position(), 0);
        QVERIFY(!w.history().canUndo());
        QCOMPARE(w.layers().count(), 1);
        QVERIFY(w.selection().isEmpty());
        QVERIFY(w.canvasView()->selectionOutline().isEmpty());
        w.undo(); // nothing on this page to undo; the other page is untouched
        stroke(w, {50, 200}, {300, 200});
        QCOMPARE(w.history().position(), 1);

        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(w.history().position(), 2);
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 2);
        QCOMPARE(w.selection().bounds(), QRect(10, 10, 50, 40));
        QVERIFY(!w.canvasView()->selectionOutline().isEmpty());
        w.deselect();
        w.undo(); // the layer
        w.undo(); // the stroke
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        w.redo();
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));

        QVERIFY(w.setCurrentPage(1));
        QCOMPARE(shown(w, 150, 200), QColor(Qt::red));
        w.undo();
        QCOMPARE(shown(w, 150, 200), QColor(Qt::white));
    }

    void theViewComesBackWhereItWasLeft()
    {
        MainWindow w;
        setupWindow(w);
        w.canvasView()->setZoomCentered(4.0);
        w.canvasView()->setPan(QPointF(120, -40));
        QVERIFY(w.addPage(QSize(64, 64), QColor(0, 0, 0, 0)));
        QVERIFY(w.canvasView()->isAutoFit()); // a page not yet looked at fits the window
        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(w.canvasView()->zoom(), 4.0);
        QCOMPARE(w.canvasView()->pan(), QPointF(120, -40));
        QVERIFY(w.setCurrentPage(1));
        QVERIFY(w.canvasView()->isAutoFit());
    }

    void switchingPlacesWhatIsFloating()
    {
        MainWindow w;
        setupWindow(w);
        stroke(w, {50, 100}, {300, 100});
        w.setSelection(Selection::rect(QRect(40, 80, 100, 40)));
        w.copy();
        QVERIFY(w.addPage(QSize(400, 300), Qt::white));
        // Copy on one page, paste on another.
        w.paste();
        QVERIFY(w.isFloating());
        QVERIFY(w.setCurrentPage(0));
        QVERIFY(!w.isFloating());
        QVERIFY(w.setCurrentPage(1));
        QCOMPARE(shown(w, 100, 100), QColor(Qt::red)); // placed where it was when the page was left
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Paste"));
    }

    void duplicateRenameMoveDelete()
    {
        MainWindow w;
        setupWindow(w);
        stroke(w, {50, 100}, {300, 100});
        QVERIFY(w.renamePage(0, QStringLiteral("  Walk  ")));
        QCOMPARE(w.pageName(0), QStringLiteral("Walk"));
        QCOMPARE(w.pageTabs()->tabText(0), QStringLiteral("Walk"));
        QVERIFY(!w.renamePage(0, QStringLiteral("   ")));

        QVERIFY(w.duplicatePage());
        QCOMPARE(w.pageCount(), 2);
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(w.pageName(1), QStringLiteral("Walk copy"));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        // The copy is its own picture.
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {150, 50}, {150, 250});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QCOMPARE(shown(w, 150, 200), QColor(Qt::white));

        QVERIFY(w.addPage(QSize(64, 64), Qt::white)); // Walk, Page 3, Walk copy
        QCOMPARE(w.pageName(1), QStringLiteral("Page 3"));
        QVERIFY(w.movePage(1, 2)); // Walk, Walk copy, Page 3
        QCOMPARE(w.currentPage(), 2); // the page being worked on is still current
        QCOMPARE(w.canvasSize(), QSize(64, 64));
        QCOMPARE(w.pageName(1), QStringLiteral("Walk copy"));
        QCOMPARE(w.pageTabs()->tabText(2), QStringLiteral("Page 3"));
        QCOMPARE(w.pageTabs()->currentIndex(), 2);

        // Deleting another page leaves the current one showing.
        QVERIFY(w.deletePage(0)); // Walk copy, Page 3
        QCOMPARE(w.pageCount(), 2);
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(w.canvasSize(), QSize(64, 64));
        // Deleting the current one shows its neighbour.
        QVERIFY(w.deletePage(1));
        QCOMPARE(w.pageCount(), 1);
        QCOMPARE(w.currentPage(), 0);
        QCOMPARE(w.pageName(0), QStringLiteral("Walk copy"));
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QVERIFY(near(drawn(w, {150, 100}), Qt::blue));
        QCOMPARE(w.pageTabs()->count(), 1);
        stroke(w, {50, 250}, {300, 250}); // and it can be painted on
        QCOMPARE(shown(w, 150, 250), QColor(Qt::blue));
    }

    void tabsSwitchAndReorderPages()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addPage(QSize(64, 64), Qt::white));
        QVERIFY(w.addPage(QSize(32, 32), Qt::white));
        QTabBar *tabs = w.pageTabs();
        QCOMPARE(tabs->count(), 3);

        // Clicking a tab shows its page.
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(0).center());
        QCOMPARE(w.currentPage(), 0);
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        tabs->setCurrentIndex(2);
        QCOMPARE(w.currentPage(), 2);
        QCOMPARE(w.canvasSize(), QSize(32, 32));

        // Dragging a tab reorders the pages.
        tabs->moveTab(2, 0);
        QCOMPARE(w.pageName(0), QStringLiteral("Page 3"));
        QCOMPARE(w.pageName(1), QStringLiteral("Page 1"));
        QCOMPARE(w.currentPage(), 0);
        QCOMPARE(w.canvasSize(), QSize(32, 32));
        QVERIFY(w.setCurrentPage(1));
        QCOMPARE(w.canvasSize(), QSize(400, 300));

        // A page can't be left in the middle of a stroke.
        CanvasView *view = w.canvasView();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 50}));
        tabs->setCurrentIndex(2);
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(tabs->currentIndex(), 1);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 50}));
    }

    void pageChangesAreUnsavedChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("project.easeletch"));
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(!w.isModified());

        QVERIFY(w.addPage(QSize(64, 64), Qt::white));
        QVERIFY(w.isModified());
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(!w.isModified());

        // Painting on a page and leaving it: still unsaved.
        stroke(w, {10, 10}, {50, 50});
        QVERIFY(w.setCurrentPage(0));
        QVERIFY(w.isModified());
        QVERIFY(w.isWindowModified());
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(!w.isModified());
        QVERIFY(w.setCurrentPage(1));
        QVERIFY(!w.isModified());
        w.undo();
        QVERIFY(w.isModified());
        w.redo();
        QVERIFY(!w.isModified());

        QVERIFY(w.renamePage(1, QStringLiteral("Icon")));
        QVERIFY(w.isModified());
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(w.movePage(1, 0));
        QVERIFY(w.isModified());
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(w.deletePage(1));
        QVERIFY(w.isModified());
    }

    void pagesAreSavedAndReopened()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("project.easeletch"));
        {
            MainWindow w;
            setupWindow(w);
            stroke(w, {50, 100}, {300, 100});
            QVERIFY(w.renamePage(0, QStringLiteral("Hero")));
            QVERIFY(w.addPage(QSize(200, 600), Qt::white));
            w.canvasView()->setZoomCentered(1.0);
            QVERIFY(w.addLayer());
            w.brushTool()->setColor(Qt::blue);
            stroke(w, {100, 50}, {100, 500});
            QVERIFY(w.addPage(QSize(64, 64), QColor(0, 0, 0, 0)));
            QVERIFY(w.setCurrentPage(1));
            // Saved in the background, while the page is left and painted on.
            QSignalSpy saved(&w, &MainWindow::documentSaved);
            QVERIFY(w.saveDocumentTo(path));
            QVERIFY(w.setCurrentPage(0));
            QVERIFY(saved.count() == 1 || saved.wait(5000));
            QCOMPARE(saved.first().at(1).toBool(), true);
            QVERIFY(!w.isModified());
        }

        MainWindow w;
        setupWindow(w);
        QSignalSpy opened(&w, &MainWindow::documentOpened);
        w.openDocument(path);
        QVERIFY(opened.wait(5000));
        QCOMPARE(opened.first().at(1).toBool(), true);
        QCOMPARE(w.pageCount(), 3);
        QCOMPARE(w.pageTabs()->count(), 3);
        QCOMPARE(w.pageName(0), QStringLiteral("Hero"));
        QCOMPARE(w.pageName(1), QStringLiteral("Page 2"));
        QCOMPARE(w.pageSize(2), QSize(64, 64));
        // It opens on the page it was saved on.
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(w.canvasSize(), QSize(200, 600));
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(shown(w, 100, 300), QColor(Qt::blue));
        QVERIFY(!w.isModified());
        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QVERIFY(near(drawn(w, {150, 100}), Qt::red));
        QVERIFY(!w.history().canUndo());
        QVERIFY(w.setCurrentPage(2));
        QCOMPARE(shown(w, 30, 30).alpha(), 0);
        QVERIFY(!w.isModified());

        // A new document starts over with one page.
        w.newDocument(QSize(100, 100), Qt::white);
        QCOMPARE(w.pageCount(), 1);
        QCOMPARE(w.pageTabs()->count(), 1);
        QCOMPARE(w.pageName(0), QStringLiteral("Page 1"));
    }

    void adjustmentLayersBelongToTheirPage()
    {
        MainWindow w;
        setupWindow(w);
        stroke(w, {50, 100}, {300, 100});
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::BlackWhite));
        QVERIFY(shown(w, 150, 100) != QColor(Qt::red)); // the red stroke shows grey
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::BlackWhite);
        // Nothing to paint on while the adjustment layer is active, and it says so.
        stroke(w, {50, 200}, {300, 200});
        QCOMPARE(shown(w, 150, 200), QColor(Qt::white));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("adjustment layer")));

        // A new page is plain: no adjustment, and the brush paints.
        QVERIFY(w.addPage(QSize(400, 300), Qt::white));
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::None);
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));

        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::BlackWhite);
        QVERIFY(shown(w, 150, 100) != QColor(Qt::red));
        const QColor grey = shown(w, 150, 100);
        QVERIFY(near(drawn(w, {150, 100}), grey));
    }

    void brushOutsideASelectionSaysWhy()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(10, 10, 40, 40)));
        stroke(w, {100, 200}, {300, 200});
        QCOMPARE(shown(w, 200, 200), QColor(Qt::white)); // painting stays inside the selection
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("selection")));
        w.statusBar()->clearMessage();
        stroke(w, {20, 20}, {40, 40});
        QCOMPARE(shown(w, 30, 30), QColor(Qt::red));
        QVERIFY(w.statusBar()->currentMessage().isEmpty());
    }
};

QTEST_MAIN(TestPages)
#include "tst_pages.moc"
