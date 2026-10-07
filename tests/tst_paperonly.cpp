#include "brushpresets.h"
#include "brushtool.h"
#include "canvasview.h"
#include "mainwindow.h"

#include <QDockWidget>
#include <QMenuBar>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

using namespace easeletch;

// View > Paper Only: nothing on screen but the drawing and a small strip.

namespace {

void saveShot(QWidget &w, const char *name)
{
    const QString dir = qEnvironmentVariable("EASELETCH_TEST_SHOTS");
    if (!dir.isEmpty())
        w.grab().save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
}

void setupWindow(MainWindow &w)
{
    w.resize(1221, 718);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::white, QStringLiteral("cotton"));
    QTest::qWait(300); // the docks settle
}

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

void stroke(MainWindow &w, QPointF from, QPointF to)
{
    CanvasView *view = w.canvasView();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, from));
    for (int i = 1; i <= 20; ++i)
        QTest::mouseMove(view, viewPos(w, from + (to - from) * (i / 20.0)));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, to));
}

QColor shown(MainWindow &w, int x, int y)
{
    w.canvasView()->refresh();
    return pixelToColor(w.layers().composite().pixel(x, y));
}

// Everything that isn't the drawing.
QList<QWidget *> chrome(MainWindow &w)
{
    QList<QWidget *> out{w.menuBar(), w.statusBar(), w.pageTabs()};
    for (QToolBar *bar : w.findChildren<QToolBar *>())
        out << bar;
    for (QDockWidget *dock : w.findChildren<QDockWidget *>())
        out << dock;
    for (QScrollBar *bar : w.canvasView()->parentWidget()->findChildren<QScrollBar *>(Qt::FindDirectChildrenOnly))
        out << bar;
    return out;
}

QList<QWidget *> visibleOf(const QList<QWidget *> &all)
{
    QList<QWidget *> out;
    for (QWidget *w : all)
        if (w->isVisible())
            out << w;
    return out;
}

} // namespace

class TestPaperOnly : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("EaseletchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_paperonly"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void everythingButTheDrawingIsPutAwayAndComesBack()
    {
        MainWindow w;
        setupWindow(w);
        const QList<QWidget *> before = visibleOf(chrome(w));
        QVERIFY(before.size() >= 8);
        const QSize viewBefore = w.canvasView()->size();
        const QByteArray layout = w.saveState();
        QVERIFY(!w.isPaperOnly());

        w.setPaperOnly(true);
        QVERIFY(w.isPaperOnly());
        QTRY_VERIFY(w.isFullScreen());
        QTest::qWait(200);
        QCOMPARE(visibleOf(chrome(w)), QList<QWidget *>());
        // The drawing has the room all of it took.
        QVERIFY(w.canvasView()->width() > viewBefore.width());
        QVERIFY(w.canvasView()->height() > viewBefore.height());
        QCOMPARE(w.canvasView()->size(), w.size());
        // ... and the strip is on it.
        auto *strip = w.findChild<QWidget *>(QStringLiteral("paperStrip"));
        QVERIFY(strip && strip->isVisible());
        QVERIFY(w.canvasView()->rect().contains(strip->geometry()));
        saveShot(w, "paper-only");

        w.setPaperOnly(false);
        QVERIFY(!w.isPaperOnly());
        QTRY_VERIFY(!w.isFullScreen());
        QTest::qWait(300);
        QCOMPARE(visibleOf(chrome(w)), before);
        QVERIFY(!strip->isVisible());
        QCOMPARE(w.saveState(), layout); // docks and toolbars where they were
        for (QDockWidget *dock : w.findChildren<QDockWidget *>())
            QVERIFY(!dock->isFloating());
    }

    void theStripDoesWhatThereIsNoKeyboardFor()
    {
        MainWindow w;
        setupWindow(w);
        w.setPaperOnly(true);
        QTRY_VERIFY(w.isFullScreen());
        QTest::qWait(200);
        const auto find = [&w](const char *name) { return w.findChild<QToolButton *>(QLatin1String(name)); };
        QToolButton *undo = find("paperUndo"), *redo = find("paperRedo"), *brush = find("paperBrush");
        QToolButton *eraser = find("paperEraser"), *color = find("paperColor"), *layers = find("paperLayers");
        QToolButton *done = find("stripDone");
        QVERIFY(undo && redo && brush && eraser && color && layers && done);
        for (QToolButton *b : {undo, redo, brush, eraser, color, layers, done})
            QVERIFY2(b->isVisible() && b->height() >= 36, qPrintable(b->objectName()));

        // Drawing works, and its undo is on the strip.
        QVERIFY(w.chooseBrushPreset(QStringLiteral("ink-tech")));
        w.brushTool()->setColor(Qt::black);
        QCOMPARE(brush->text(), QStringLiteral("Tech pen"));
        QVERIFY(brush->isChecked() && !eraser->isChecked());
        QVERIFY(!undo->isEnabled());
        const QColor bare = shown(w, 200, 150);
        stroke(w, {100, 150}, {300, 150});
        QVERIFY(shown(w, 200, 150).lightness() < 60);
        QVERIFY(undo->isEnabled());
        QTest::mouseClick(undo, Qt::LeftButton);
        QCOMPARE(shown(w, 200, 150), bare);
        QTest::mouseClick(redo, Qt::LeftButton);
        QVERIFY(shown(w, 200, 150).lightness() < 60);

        // Eraser and back.
        QTest::mouseClick(eraser, Qt::LeftButton);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Erase);
        QVERIFY(eraser->isChecked() && !brush->isChecked());
        QTest::mouseClick(brush, Qt::LeftButton); // from the eraser it takes up the brush; no list
        QCOMPARE(w.brushTool()->mode(), BrushMode::Paint);
        QVERIFY(brush->isChecked());
        // No options bar crept back with the change of tool.
        for (QToolBar *bar : w.findChildren<QToolBar *>())
            QVERIFY2(!bar->isVisible(), qPrintable(bar->objectName()));

        // The Color and Layers panels come up as windows of their own.
        auto *colorDock = w.findChild<QDockWidget *>(QStringLiteral("ColorDock"));
        auto *layersDock = w.findChild<QDockWidget *>(QStringLiteral("LayersDock"));
        QVERIFY(!colorDock->isVisible());
        QTest::mouseClick(color, Qt::LeftButton);
        QVERIFY(colorDock->isVisible() && colorDock->isFloating());
        QTest::mouseClick(layers, Qt::LeftButton);
        QVERIFY(layersDock->isVisible() && layersDock->isFloating());
        saveShot(w, "paper-only-panels");
        QTest::mouseClick(color, Qt::LeftButton);
        QVERIFY(!colorDock->isVisible());

        // Done: back to the window, panels docked again.
        QTest::mouseClick(done, Qt::LeftButton);
        QVERIFY(!w.isPaperOnly());
        QTest::qWait(300);
        QVERIFY(colorDock->isVisible() || !colorDock->visibleRegion().isNull() || !colorDock->isFloating());
        QVERIFY(!colorDock->isFloating() && !layersDock->isFloating());
        QVERIFY(layersDock->isVisible());
        QVERIFY(w.findChild<QToolBar *>(QStringLiteral("ToolOptionsBar"))->isVisible());
    }

    void theStripCanBeDraggedButNotOffThePaper()
    {
        MainWindow w;
        setupWindow(w);
        w.setPaperOnly(true);
        QTRY_VERIFY(w.isFullScreen());
        QTest::qWait(200);
        auto *strip = w.findChild<QWidget *>(QStringLiteral("paperStrip"));
        QVERIFY(strip);
        const QPoint start = strip->pos();
        const QPoint grip(4, strip->height() / 2); // its edge, clear of the buttons
        QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(strip, grip + QPoint(150, 90));
        QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, grip + QPoint(150, 90));
        QCOMPARE(strip->pos(), start + QPoint(150, 90));
        // Flung far past the corner, it stops at the edge.
        QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(strip, grip + QPoint(9000, 9000));
        QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, grip + QPoint(9000, 9000));
        QVERIFY(w.canvasView()->rect().contains(strip->geometry()));
        w.setPaperOnly(false);
    }

    void closingFromPaperOnlyKeepsTheWorkingLayout()
    {
        QByteArray layout;
        {
            MainWindow w;
            setupWindow(w);
            layout = w.saveState();
            w.setPaperOnly(true);
            QTRY_VERIFY(w.isFullScreen());
            w.close();
        }
        MainWindow w;
        setupWindow(w);
        QVERIFY(!w.isPaperOnly());
        QVERIFY(w.menuBar()->isVisible());
        QVERIFY(w.findChild<QDockWidget *>(QStringLiteral("LayersDock"))->isVisible());
        QVERIFY(w.findChild<QToolBar *>(QStringLiteral("ToolsBar"))->isVisible());
    }
};

QTEST_MAIN(TestPaperOnly)
#include "tst_paperonly.moc"
