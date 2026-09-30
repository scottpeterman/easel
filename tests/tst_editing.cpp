#include "brushtool.h"
#include "canvasview.h"
#include "color.h"
#include "mainwindow.h"
#include "selecttools.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

using namespace easel;

namespace {

QColor pixel(MainWindow &w, int x, int y)
{
    const QColor c = pixelToColor(w.layer()->pixel(x, y));
    return QColor(c.red(), c.green(), c.blue(), c.alpha());
}

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

// 400 x 300 white canvas at 100%, a red 20 x 20 square at (50, 90).
void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w)); // shortcuts need an active window
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    w.layer()->fillRect(QRect(50, 90, 20, 20), QColor(Qt::red));
    w.canvasView()->refresh();
    w.canvasView()->setFocus();
}

void drag(MainWindow &w, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    CanvasView *view = w.canvasView();
    QTest::mousePress(view, Qt::LeftButton, mods, viewPos(w, from));
    for (int i = 1; i <= 8; ++i)
        QTest::mouseMove(view, viewPos(w, from + (to - from) * (i / 8.0)));
    QTest::mouseRelease(view, Qt::LeftButton, mods, viewPos(w, to));
}

void key(MainWindow &w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QTest::keyClick(w.canvasView(), Qt::Key(k), mods);
}

} // namespace

class TestEditing : public QObject
{
    Q_OBJECT

private slots:
    void init() { QGuiApplication::clipboard()->clear(); }

    void rectSelectShowsAnts()
    {
        MainWindow w;
        setupWindow(w);
        key(w, Qt::Key_M);
        QCOMPARE(w.canvasView()->tool(), w.rectSelectTool());
        drag(w, {40, 80}, {80, 120});
        QCOMPARE(w.selection().shape(), Selection::Shape::Rect);
        QCOMPARE(w.selection().bounds(), QRect(40, 80, 40, 40));
        QVERIFY(!w.canvasView()->selectionOutline().isEmpty());

        // A click without a drag deselects.
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 200}));
        QVERIFY(w.selection().isEmpty());
        QVERIFY(w.canvasView()->selectionOutline().isEmpty());
    }

    void selectAllAndDeselectShortcuts()
    {
        MainWindow w;
        setupWindow(w);
        key(w, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(w.selection().bounds(), QRect(0, 0, 400, 300));
        key(w, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(w.selection().isEmpty());
    }

    void copyPasteDragCommit()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        w.copy();
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());

        w.paste();
        QVERIFY(w.isFloating());
        QCOMPARE(w.canvasView()->tool(), w.moveTool());
        QCOMPARE(w.floatingPosition(), QPoint(50, 90)); // back where it came from

        drag(w, {60, 100}, {160, 150});
        QCOMPARE(w.floatingPosition(), QPoint(150, 140));
        QCOMPARE(w.selection().bounds(), QRect(150, 140, 20, 20));
        QCOMPARE(pixel(w, 160, 150), QColor(Qt::red)); // shown while floating

        key(w, Qt::Key_Return);
        QVERIFY(!w.isFloating());
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Paste"));
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));   // original kept
        QCOMPARE(pixel(w, 160, 150), QColor(Qt::red));  // copy placed
        QCOMPARE(pixel(w, 171, 150), QColor(Qt::white));

        w.undo();
        QCOMPARE(pixel(w, 160, 150), QColor(Qt::white));
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
    }

    void cutClearsAndPastesBack()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(pixel(w, 60, 100).alpha(), 0);
        QCOMPARE(pixel(w, 49, 100), QColor(Qt::white));
        QCOMPARE(w.history().label(0), QStringLiteral("Cut"));

        key(w, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(w.isFloating());
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
        w.commitFloating();
        QCOMPARE(w.history().count(), 2);

        w.undo(); // the paste
        QCOMPARE(pixel(w, 60, 100).alpha(), 0);
        w.undo(); // the cut
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
    }

    void deleteClearsEllipseOnly()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::ellipse(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_Delete);
        QCOMPARE(pixel(w, 60, 100).alpha(), 0);          // centre
        QCOMPARE(pixel(w, 50, 90), QColor(Qt::red));     // corner is outside the ellipse
        QCOMPARE(w.history().label(0), QStringLiteral("Delete"));
    }

    void moveToolNudgesAndEscapeRestores()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_V);
        QCOMPARE(w.canvasView()->tool(), w.moveTool());

        key(w, Qt::Key_Right, Qt::ShiftModifier);
        key(w, Qt::Key_Down);
        QVERIFY(w.isFloating());
        QCOMPARE(w.floatingPosition(), QPoint(60, 91));
        QCOMPARE(pixel(w, 55, 100).alpha(), 0); // the hole it left
        QCOMPARE(pixel(w, 75, 100), QColor(Qt::red));

        key(w, Qt::Key_Escape);
        QVERIFY(!w.isFloating());
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(pixel(w, 55, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 75, 100), QColor(Qt::white));
        QCOMPARE(w.selection().bounds(), QRect(50, 90, 20, 20));

        // Escape with nothing floating deselects.
        key(w, Qt::Key_Escape);
        QVERIFY(w.selection().isEmpty());
    }

    void moveCommitsOnToolSwitchAndUndoes()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_V);
        drag(w, {60, 100}, {260, 100});
        QVERIFY(w.isFloating());
        key(w, Qt::Key_B); // switching tools drops it
        QVERIFY(!w.isFloating());
        QCOMPARE(w.history().label(0), QStringLiteral("Move"));
        QCOMPARE(pixel(w, 260, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 60, 100).alpha(), 0); // lifted: leaves a hole

        w.undo();
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 260, 100), QColor(Qt::white));
    }

    void undoWhileFloatingCancels()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_V);
        drag(w, {60, 100}, {200, 200});
        QVERIFY(w.isFloating());
        QCOMPARE(pixel(w, 200, 200), QColor(Qt::red));
        w.undo();
        QVERIFY(!w.isFloating());
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
    }

    void pastesExternalImageInView()
    {
        MainWindow w;
        setupWindow(w);
        QImage img(10, 10, QImage::Format_ARGB32);
        img.fill(Qt::blue);
        QGuiApplication::clipboard()->setImage(img);
        w.paste();
        QVERIFY(w.isFloating());
        const QPoint p = w.floatingPosition();
        QCOMPARE(w.selection().bounds(), QRect(p, QSize(10, 10)));
        w.commitFloating();
        QCOMPARE(pixel(w, p.x() + 5, p.y() + 5), QColor(Qt::blue));
    }

    void brushStaysInsideSelection()
    {
        MainWindow w;
        setupWindow(w);
        BrushSettings b = w.brushTool()->settings();
        b.size = 10;
        b.hardness = 1.0;
        b.opacity = 1.0;
        b.flow = 1.0;
        b.stabilizer = 0.0;
        w.brushTool()->setMode(BrushMode::Paint);
        w.brushTool()->setSettings(b);
        w.brushTool()->setColor(Qt::green);
        w.setSelection(Selection::ellipse(QRect(200, 100, 100, 100)));
        drag(w, {150, 150}, {350, 150});
        QCOMPARE(pixel(w, 250, 150), QColor(Qt::green));
        QCOMPARE(pixel(w, 190, 150), QColor(Qt::white));
        QCOMPARE(pixel(w, 310, 150), QColor(Qt::white));
    }

    void spriteGridSnapsSelections()
    {
        MainWindow w;
        setupWindow(w);
        GridSettings g;
        g.cells = true;
        g.cell = QSize(32, 24);
        g.offset = QPoint(4, 2);
        g.snap = true;
        w.setGridSettings(g);
        QVERIFY(w.canvasView()->cellGridVisible());

        key(w, Qt::Key_M);
        // A click selects the cell under it.
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 40}));
        QCOMPARE(w.selection().bounds(), QRect(36, 26, 32, 24));
        // A drag covers every cell it touches.
        drag(w, {50, 40}, {110, 70});
        QCOMPARE(w.selection().bounds(), QRect(36, 26, 96, 48));

        g.cells = false; // hidden grid: no snapping
        w.setGridSettings(g);
        drag(w, {50, 40}, {110, 70});
        QCOMPARE(w.selection().bounds(), QRect(50, 40, 60, 30));
    }

    void cropToSelectionAndUndo()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(45, 85, 30, 30)));
        key(w, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(w.canvasSize(), QSize(30, 30));
        QCOMPARE(w.canvasView()->canvasSize(), QSize(30, 30));
        QVERIFY(w.selection().isEmpty());
        QCOMPARE(pixel(w, 5, 5), QColor(Qt::red));
        QCOMPARE(pixel(w, 4, 5), QColor(Qt::white));
        QVERIFY(w.windowTitle().contains(QStringLiteral("30 × 30")));

        // Painting is bounded by the new canvas.
        w.brushTool()->setColor(Qt::green);
        drag(w, {-10, 2}, {40, 2});
        QCOMPARE(pixel(w, 29, 2), QColor(Qt::green));

        w.undo(); // the stroke
        w.undo(); // the crop
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        QCOMPARE(w.canvasView()->canvasSize(), QSize(400, 300));
        QCOMPARE(pixel(w, 50, 90), QColor(Qt::red));
        QCOMPARE(pixel(w, 5, 5), QColor(Qt::white));
        w.redo();
        QCOMPARE(w.canvasSize(), QSize(30, 30));
    }

    void exportSelectionWritesJustThosePixels()
    {
        MainWindow w;
        setupWindow(w);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("frame.png"));
        w.setSelection(Selection::ellipse(QRect(50, 90, 20, 20)));
        QVERIFY(w.exportSelectionTo(path));
        const QImage img(path);
        QCOMPARE(img.size(), QSize(20, 20));
        QCOMPARE(img.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(img.pixelColor(0, 0).alpha(), 0); // outside the ellipse

        // Partly off the canvas: only the part on it.
        w.setSelection(Selection::rect(QRect(390, 290, 30, 30)));
        QVERIFY(w.exportSelectionTo(path));
        QCOMPARE(QImage(path).size(), QSize(10, 10));
    }

    void wandSelectsAndCombines()
    {
        MainWindow w;
        setupWindow(w);
        w.layer()->fillRect(QRect(200, 90, 10, 10), QColor(Qt::red));
        w.canvasView()->refresh();
        key(w, Qt::Key_W);
        QCOMPARE(w.canvasView()->tool(), w.wandTool());
        QVERIFY(w.findChild<QToolBar *>(QStringLiteral("WandOptionsBar"))->isVisible());

        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {60, 100}));
        QCOMPARE(w.selection().shape(), Selection::Shape::Mask);
        QCOMPARE(w.selection().bounds(), QRect(50, 90, 20, 20));
        QVERIFY(!w.canvasView()->selectionOutline().isEmpty());

        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {205, 95}));
        QCOMPARE(w.selection().bounds(), QRect(50, 90, 160, 20));
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::ControlModifier, viewPos(w, {60, 100}));
        QCOMPARE(w.selection().bounds(), QRect(200, 90, 10, 10));

        // Wand the background, invert: the two squares.
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {5, 5}));
        key(w, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(w.selection().contains(60, 100));
        QVERIFY(w.selection().contains(205, 95));
        QVERIFY(!w.selection().contains(5, 5));

        // Brush clipped to a wand selection.
        key(w, Qt::Key_B);
        QVERIFY(!w.findChild<QToolBar *>(QStringLiteral("WandOptionsBar"))->isVisible());
    }

    void backgroundToAlphaThenTrim()
    {
        MainWindow w;
        setupWindow(w);
        w.colorToAlpha(Qt::white, 0.02);
        QCOMPARE(pixel(w, 5, 5).alpha(), 0);
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
        QCOMPARE(w.history().label(0), QStringLiteral("Color to Alpha"));

        key(w, Qt::Key_T, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w.canvasSize(), QSize(20, 20));
        QCOMPARE(pixel(w, 0, 0), QColor(Qt::red));
        QVERIFY(w.history().label(1).startsWith(QStringLiteral("Trim")));

        w.undo();
        w.undo();
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        QCOMPARE(pixel(w, 5, 5), QColor(Qt::white));
    }

    void spriteCleanupWorkflow()
    {
        // Wand the background, grow into the fringe, Color to Alpha there only,
        // then Trim: the sprite keeps its colour, the fringe is unmixed.
        MainWindow w;
        setupWindow(w);
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::black));
        w.layer()->fillRect(QRect(100, 100, 20, 20), QColor(120, 30, 30)); // dark sprite
        w.layer()->fillRect(QRect(99, 100, 1, 20), QColor(60, 15, 15));    // soft left edge
        w.canvasView()->refresh();
        key(w, Qt::Key_W);
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {5, 5}));
        QVERIFY(w.selection().contains(99, 100) == false); // fringe outside a tight wand
        w.growSelection(1);
        QVERIFY(w.selection().contains(99, 100));
        QVERIFY(!w.selection().contains(110, 110));
        w.colorToAlpha(Qt::black, 0.04);
        w.deselect();
        w.trim();
        QCOMPARE(w.canvasSize(), QSize(21, 20));
        QCOMPARE(pixel(w, 11, 10), QColor(120, 30, 30)); // body untouched
        const QColor edge = pixel(w, 0, 10);
        QVERIFY2(edge.alpha() > 0 && edge.alpha() < 255 && edge.red() > 200, qPrintable(edge.name(QColor::HexArgb)));
    }

    void wandDeleteMakesBackgroundTransparent()
    {
        MainWindow w;
        setupWindow(w);
        key(w, Qt::Key_W);
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {5, 5}));
        key(w, Qt::Key_Delete);
        QCOMPARE(pixel(w, 5, 5).alpha(), 0);
        QCOMPARE(pixel(w, 399, 299).alpha(), 0);
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
    }

    void smudgeToolDragsColour()
    {
        MainWindow w;
        setupWindow(w);
        key(w, Qt::Key_S);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Smudge);
        QCOMPARE(w.canvasView()->tool(), w.brushTool());
        drag(w, {60, 100}, {100, 100});
        const QColor c = pixel(w, 80, 100);
        QVERIFY2(c.green() < 250 && c.red() == 255, qPrintable(c.name()));
        QCOMPARE(w.history().label(0), QStringLiteral("Smudge"));
    }
};

QTEST_MAIN(TestEditing)
#include "tst_editing.moc"
