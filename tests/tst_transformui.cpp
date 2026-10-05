#include "brushtool.h"
#include "canvasview.h"
#include "color.h"
#include "edittools.h"
#include "mainwindow.h"
#include "selecttools.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QTest>

using namespace easeletch;

namespace {

QColor pixel(MainWindow &w, int x, int y)
{
    const QColor c = pixelToColor(w.layer()->pixel(x, y));
    return QColor(c.red(), c.green(), c.blue(), c.alpha());
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

// A second, transparent layer with a 20 x 10 block at (100, 100): left half
// red, right half blue.
void addSpriteLayer(MainWindow &w)
{
    QVERIFY(w.addLayer());
    w.layer()->fillRect(QRect(100, 100, 10, 10), QColor(Qt::red));
    w.layer()->fillRect(QRect(110, 100, 10, 10), QColor(Qt::blue));
    w.canvasView()->refresh();
}

void sendMouse(QWidget *target, QEvent::Type type, const QPointF &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent e(type, pos, target->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &e);
}

void drag(MainWindow &w, QPointF from, QPointF to)
{
    CanvasView *view = w.canvasView();
    const QTransform xf = view->canvasToView();
    sendMouse(view, QEvent::MouseButtonPress, xf.map(from), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i)
        sendMouse(view, QEvent::MouseMove, xf.map(from + (to - from) * (i / 8.0)), Qt::NoButton,
                  Qt::LeftButton);
    sendMouse(view, QEvent::MouseButtonRelease, xf.map(to), Qt::LeftButton, Qt::NoButton);
}

void key(MainWindow &w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QTest::keyClick(w.canvasView(), Qt::Key(k), mods);
}

} // namespace

class TestTransformUi : public QObject
{
    Q_OBJECT

private slots:
    void init() { QGuiApplication::clipboard()->clear(); }

    void scaleByCornerThenEnter()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(50, 90, 20, 20)));
        key(w, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(w.isTransforming());
        QCOMPARE(w.canvasView()->tool(), w.transformTool());
        // The box, with a handle on each corner; too small on screen for more.
        QCOMPARE(w.canvasView()->selectionOutline().size(), 1);
        QCOMPARE(w.canvasView()->handles().size(), 4);

        drag(w, {70, 110}, {90, 130}); // bottom-right corner, twice the size
        QCOMPARE(w.canvasView()->handles().size(), 8); // room for the side handles now
        QCOMPARE(w.transformBox().scaleX, 2.0);
        QCOMPARE(w.transformBox().scaleY, 2.0);
        QCOMPARE(w.history().count(), 0); // nothing recorded until it's applied

        key(w, Qt::Key_Return);
        QVERIFY(!w.isTransforming());
        QVERIFY(!w.isFloating());
        QVERIFY(w.canvasView()->handles().isEmpty());
        QCOMPARE(w.canvasView()->tool(), w.brushTool()); // back to the tool in use before
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Transform"));
        QCOMPARE(w.selection().bounds(), QRect(50, 90, 40, 40));
        // Solid right to the edge, and the top-left corner hasn't moved.
        QCOMPARE(pixel(w, 50, 90), QColor(Qt::red));
        QCOMPARE(pixel(w, 89, 129), QColor(Qt::red));
        QCOMPARE(pixel(w, 90, 130), QColor(Qt::white));

        w.undo();
        QCOMPARE(pixel(w, 89, 129), QColor(Qt::white));
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
        w.redo();
        QCOMPARE(pixel(w, 89, 129), QColor(Qt::red));
    }

    void escapePutsEverythingBack()
    {
        MainWindow w;
        setupWindow(w);
        const Selection sel = Selection::rect(QRect(50, 90, 20, 20));
        w.setSelection(sel);
        key(w, Qt::Key_E); // the eraser was in use
        key(w, Qt::Key_T, Qt::ControlModifier);
        drag(w, {70, 110}, {120, 160});
        drag(w, {200, 100}, {200, 180}); // outside the box: rotates
        QVERIFY(std::abs(w.transformBox().angle) > 1.0);
        key(w, Qt::Key_Escape);
        QVERIFY(!w.isTransforming());
        QVERIFY(!w.isFloating());
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(w.selection(), sel);
        QCOMPARE(pixel(w, 60, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 100, 140), QColor(Qt::white));
        QCOMPARE(w.canvasView()->tool(), w.brushTool());
        QCOMPARE(w.brushTool()->mode(), BrushMode::Erase);
    }

    void wholeLayerWhenNothingIsSelected()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        QVERIFY(w.beginTransform());
        // The box fits what's on the layer, not the canvas.
        QCOMPARE(w.transformBox().size, QSizeF(20, 10));
        QCOMPARE(w.transformBox().center, QPointF(110, 105));
        w.setTransform(1.0, 1.0, 90.0);
        w.commitFloating();
        QVERIFY(w.selection().isEmpty()); // nothing was selected, nothing is
        // 20 x 10 turned clockwise about (110, 105): 10 x 20, red on top.
        QCOMPARE(pixel(w, 105, 95), QColor(Qt::red));
        QCOMPARE(pixel(w, 114, 104), QColor(Qt::red));
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::blue));
        QCOMPARE(pixel(w, 114, 114), QColor(Qt::blue));
        QCOMPARE(pixel(w, 102, 102).alpha(), 0);
        QCOMPARE(pixel(w, 117, 102).alpha(), 0);
    }

    void flipIsOneStepOfItsOwn()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        const qsizetype steps = w.history().count();
        w.flipHorizontal();
        QVERIFY(!w.isTransforming());
        QVERIFY(!w.isFloating());
        QCOMPARE(w.canvasView()->tool(), w.brushTool());
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().label(steps), QStringLiteral("Flip Horizontal"));
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::blue));
        QCOMPARE(pixel(w, 115, 105), QColor(Qt::red));
        QVERIFY(w.selection().isEmpty());

        w.rotateQuarter(-1);
        QCOMPARE(w.history().label(steps + 1), QStringLiteral("Rotate 90° Left"));
        QCOMPARE(pixel(w, 110, 110), QColor(Qt::blue)); // counter-clockwise: the left end is now the bottom
        QCOMPARE(pixel(w, 110, 99), QColor(Qt::red));

        w.undo();
        w.undo();
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::red));
        QCOMPARE(pixel(w, 115, 105), QColor(Qt::blue));
    }

    void flipWithinASelection()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        w.setSelection(Selection::rect(QRect(100, 100, 20, 10)));
        w.flipVertical(); // symmetrical top to bottom: looks the same
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::red));
        w.flipHorizontal();
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::blue));
        QCOMPARE(w.selection().bounds(), QRect(100, 100, 20, 10)); // still selected
    }

    void hardPixelsForSprites()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        w.setTransformSmooth(false);
        QVERIFY(w.beginTransform());
        w.setTransform(1.5, 1.5, 0.0);
        w.commitFloating();
        // 30 x 15 about (110, 105). Every pixel is one of the two colours or empty.
        int red = 0, blue = 0;
        for (int y = 90; y < 125; ++y) {
            for (int x = 90; x < 135; ++x) {
                const QColor c = pixel(w, x, y);
                QVERIFY2(c.alpha() == 0 || c == QColor(Qt::red) || c == QColor(Qt::blue),
                         qPrintable(c.name(QColor::HexArgb)));
                red += c == QColor(Qt::red);
                blue += c == QColor(Qt::blue);
            }
        }
        QCOMPARE(red, 15 * 15);
        QCOMPARE(blue, 15 * 15);
        w.setTransformSmooth(true);
    }

    void smoothRotationBlendsTheEdge()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        QVERIFY(w.beginTransform());
        w.setTransform(3.0, 3.0, 20.0);
        w.commitFloating();
        int partial = 0;
        for (int y = 60; y < 150; ++y)
            for (int x = 60; x < 160; ++x)
                partial += pixel(w, x, y).alpha() > 5 && pixel(w, x, y).alpha() < 250;
        QVERIFY(partial > 20);
        QCOMPARE(pixel(w, 110, 105).alpha(), 255);
    }

    void undoDuringATransformCancelsIt()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        const qsizetype steps = w.history().count();
        key(w, Qt::Key_T, Qt::ControlModifier);
        drag(w, {120, 110}, {160, 130});
        key(w, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.history().count(), steps);
        QCOMPARE(w.history().position(), steps); // the layer is still there
        QCOMPARE(pixel(w, 115, 105), QColor(Qt::blue));
        QCOMPARE(pixel(w, 140, 118).alpha(), 0);
    }

    void pickingAnotherToolApplies()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        const qsizetype steps = w.history().count();
        key(w, Qt::Key_T, Qt::ControlModifier);
        w.setTransform(2.0, 2.0, 0.0);
        key(w, Qt::Key_M);
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.canvasView()->tool(), w.rectSelectTool());
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(pixel(w, 95, 98), QColor(Qt::red));
        QCOMPARE(pixel(w, 128, 112), QColor(Qt::blue));
    }

    void nothingToTransformOnAnEmptyLayer()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addLayer());
        key(w, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.canvasView()->tool(), w.brushTool());
    }

    void arrowsNudgeAndDragInsideMoves()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        key(w, Qt::Key_T, Qt::ControlModifier);
        drag(w, {110, 105}, {150, 125}); // inside the box: moves it
        QCOMPARE(w.transformBox().center, QPointF(150, 125));
        key(w, Qt::Key_Right);
        key(w, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(w.transformBox().center, QPointF(151, 135));
        key(w, Qt::Key_Return);
        QCOMPARE(pixel(w, 146, 135), QColor(Qt::red));
        QCOMPARE(pixel(w, 156, 135), QColor(Qt::blue));
        QCOMPARE(pixel(w, 105, 105).alpha(), 0);
    }

    void aPasteCanBeTransformedBeforeItsPlaced()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        w.setSelection(Selection::rect(QRect(100, 100, 20, 10)));
        w.copy();
        w.paste();
        QVERIFY(w.isFloating());
        const qsizetype steps = w.history().count();
        w.flipHorizontal(); // still floating, now mirrored
        QVERIFY(w.isFloating());
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.history().count(), steps);
        key(w, Qt::Key_Return);
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().label(steps), QStringLiteral("Paste"));
        QCOMPARE(pixel(w, 105, 105), QColor(Qt::blue));
    }

    void savingAppliesTheTransform()
    {
        MainWindow w;
        setupWindow(w);
        addSpriteLayer(w);
        QVERIFY(w.beginTransform());
        w.setTransform(-1.0, 1.0, 0.0);
        QVERIFY(w.addLayer()); // any change to the layers applies it first
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.history().label(w.history().count() - 2), QStringLiteral("Transform"));
    }

    void cornersCanBeDraggedOnTheirOwn()
    {
        MainWindow w;
        setupWindow(w);
        // A second layer with a 100 x 60 block at (100, 100): red left half, blue right.
        QVERIFY(w.addLayer());
        w.layer()->fillRect(QRect(100, 100, 50, 60), QColor(Qt::red));
        w.layer()->fillRect(QRect(150, 100, 50, 60), QColor(Qt::blue));
        w.canvasView()->refresh();
        const qsizetype steps = w.history().count();
        auto *corners = w.findChild<QCheckBox *>(QStringLiteral("transformWarp"));
        QVERIFY(corners);

        QVERIFY(w.beginTransform());
        QVERIFY(!w.transformWarp());
        QVERIFY(corners->isEnabled() && !corners->isChecked());
        QCOMPARE(w.canvasView()->handles().size(), 8);
        // Turned on, it starts from the box as it is: four corners, nothing moved.
        corners->setChecked(true);
        QVERIFY(w.transformWarp());
        QCOMPARE(w.canvasView()->handles().size(), 4);
        const QPolygonF box = w.transformCorners();
        QCOMPARE(box, QPolygonF({{100, 100}, {200, 100}, {200, 160}, {100, 160}}));
        QCOMPARE(pixel(w, 120, 130), QColor(Qt::red));
        QCOMPARE(pixel(w, 180, 130), QColor(Qt::blue));

        // Dragging one corner moves that corner only; the picture follows.
        drag(w, {200, 100}, {260, 120});
        const QPolygonF pulled = w.transformCorners();
        QVERIFY(qAbs(pulled.at(1).x() - 260) <= 1.5 && qAbs(pulled.at(1).y() - 120) <= 1.5);
        QCOMPARE(pulled.at(0), box.at(0));
        QCOMPARE(pulled.at(2), box.at(2));
        QCOMPARE(pulled.at(3), box.at(3));
        QCOMPARE(pixel(w, 240, 125), QColor(Qt::blue)); // stretched out to the corner
        QCOMPARE(pixel(w, 190, 104).alpha(), 0);        // the top edge slants down now
        QCOMPARE(pixel(w, 110, 150), QColor(Qt::red));
        QVERIFY(w.isFloating());
        QCOMPARE(w.history().count(), steps); // nothing recorded yet

        // A corner can't be dragged across the others: it stays where it last made sense.
        QVERIFY(!w.setTransformCorner(0, {250, 170}));
        QCOMPARE(w.transformCorners(), pulled);
        drag(w, {100, 100}, {250, 170});
        QVERIFY(isWarpable(w.transformCorners()));
        QVERIFY(w.transformCorners().at(0).x() < 250);
        QVERIFY(w.setTransformCorner(0, box.at(0)));

        // Dragging inside moves all four; the arrows nudge them.
        drag(w, {150, 130}, {160, 150});
        const QPointF shift = w.transformCorners().at(0) - box.at(0);
        QVERIFY(qAbs(shift.x() - 10) <= 1 && qAbs(shift.y() - 20) <= 1);
        QCOMPARE(w.transformCorners().at(2), box.at(2) + shift);
        key(w, Qt::Key_Left);
        QCOMPARE(w.transformCorners().at(2), box.at(2) + shift + QPointF(-1, 0));
        // Flips and sizes belong to the box: they're off while the corners are free.
        const QPolygonF before = w.transformCorners();
        w.flipHorizontal();
        w.setTransform(2.0, 2.0, 0.0);
        QCOMPARE(w.transformCorners(), before);

        // Enter applies it as one step; undo puts the block back as it was.
        key(w, Qt::Key_Return);
        QVERIFY(!w.isTransforming());
        QVERIFY(!w.isFloating());
        QVERIFY(!corners->isChecked());
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Transform"));
        QCOMPARE(pixel(w, 250 + int(shift.x()) - 12, 125 + int(shift.y())), QColor(Qt::blue));
        w.undo();
        QCOMPARE(pixel(w, 120, 130), QColor(Qt::red));
        QCOMPARE(pixel(w, 180, 130), QColor(Qt::blue));
        QCOMPARE(pixel(w, 240, 125).alpha(), 0);

        // Turned off again before applying, it's the box once more; Escape drops everything.
        QVERIFY(w.beginTransform());
        w.setTransformWarp(true);
        QVERIFY(w.setTransformCorner(2, {300, 250}));
        QCOMPARE(pixel(w, 270, 220), QColor(Qt::blue));
        w.setTransformWarp(false);
        QCOMPARE(w.transformCorners(), box);
        QCOMPARE(w.canvasView()->handles().size(), 8);
        QCOMPARE(pixel(w, 270, 220).alpha(), 0);
        QCOMPARE(pixel(w, 180, 130), QColor(Qt::blue));
        w.setTransformWarp(true);
        QVERIFY(w.setTransformCorner(2, {300, 250}));
        key(w, Qt::Key_Escape);
        QVERIFY(!w.isTransforming());
        QCOMPARE(w.history().position(), steps); // still where the undo left it
        QCOMPARE(pixel(w, 270, 220).alpha(), 0);
        QCOMPARE(pixel(w, 180, 130), QColor(Qt::blue));

        // A selection is warped on its own, and what's left behind is cleared.
        w.setSelection(Selection::rect(QRect(150, 100, 50, 60)));
        QVERIFY(w.beginTransform());
        w.setTransformWarp(true);
        QCOMPARE(w.transformCorners(), QPolygonF({{150, 100}, {200, 100}, {200, 160}, {150, 160}}));
        QVERIFY(w.setTransformCorner(1, {280, 60}));
        QVERIFY(w.setTransformCorner(2, {280, 200}));
        key(w, Qt::Key_Return);
        QCOMPARE(pixel(w, 260, 130), QColor(Qt::blue));
        QCOMPARE(pixel(w, 120, 130), QColor(Qt::red)); // the red half never moved
        QCOMPARE(pixel(w, 260, 70).alpha() > 0, true);
        QVERIFY(!w.selection().isEmpty());             // and it's what's selected now
        QVERIFY(w.selection().contains(260, 130));
        QVERIFY(!w.selection().contains(160, 80));
    }
};

QTEST_MAIN(TestTransformUi)
#include "tst_transformui.moc"
