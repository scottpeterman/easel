#include "brushtool.h"
#include "canvasview.h"
#include "color.h"
#include "colorpanel.h"
#include "edittools.h"
#include "mainwindow.h"

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

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

// 400 x 300 white canvas at 100%, with a black 2 px ring from (100, 100) to (200, 200).
void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w)); // shortcuts need an active window
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    w.layer()->fillRect(QRect(100, 100, 100, 100), QColor(Qt::black));
    w.layer()->fillRect(QRect(102, 102, 96, 96), QColor(Qt::white));
    w.canvasView()->refresh();
    w.canvasView()->setFocus();
    w.setFillOptions({});
    w.setGradientOptions({});
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

class TestFillUi : public QObject
{
    Q_OBJECT

private slots:
    void clickFillsInsideTheOutline()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(QColor(0, 160, 0));
        key(w, Qt::Key_G);
        QCOMPARE(w.canvasView()->tool(), w.fillTool());
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {150.5, 150.5}));
        QCOMPARE(pixel(w, 150, 150), QColor(0, 160, 0));
        QCOMPARE(pixel(w, 102, 102), QColor(0, 160, 0));
        QCOMPARE(pixel(w, 101, 101), QColor(Qt::black));
        QCOMPARE(pixel(w, 50, 50), QColor(Qt::white)); // outside the ring
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Fill"));
        QCOMPARE(w.colorPanel()->recentColors().value(0), QColor(0, 160, 0));

        // Clicking it again changes nothing and records nothing.
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {150.5, 150.5}));
        QCOMPARE(w.history().count(), 1);

        w.undo();
        QCOMPARE(pixel(w, 150, 150), QColor(Qt::white));
    }

    void fillStaysInsideTheSelection()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::red);
        w.setSelection(Selection::rect(QRect(120, 120, 30, 30)));
        w.fillAt(QPoint(130, 130));
        QCOMPARE(pixel(w, 130, 130), QColor(Qt::red));
        QCOMPARE(pixel(w, 160, 160), QColor(Qt::white)); // same area, not selected
        // A click outside the selection does nothing.
        w.fillAt(QPoint(160, 160));
        QCOMPARE(pixel(w, 160, 160), QColor(Qt::white));
        QCOMPARE(w.history().count(), 1);
    }

    void notContiguousFillsTheColourEverywhere()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::blue);
        MainWindow::FillOptions o;
        o.contiguous = false;
        w.setFillOptions(o);
        w.fillAt(QPoint(150, 150));
        QCOMPARE(pixel(w, 150, 150), QColor(Qt::blue));
        QCOMPARE(pixel(w, 50, 50), QColor(Qt::blue)); // outside the ring too
        QCOMPARE(pixel(w, 100, 100), QColor(Qt::black));
    }

    void allLayersFillsUnderLineArtOnAnotherLayer()
    {
        // The ring is on the background; the colour goes on a new, empty layer.
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addLayer());
        w.colorPanel()->setColor(Qt::red);

        // Looking at this layer alone, everything is one empty area.
        w.fillAt(QPoint(150, 150));
        QCOMPARE(pixel(w, 50, 50), QColor(Qt::red));
        w.undo();

        MainWindow::FillOptions o;
        o.allLayers = true;
        w.setFillOptions(o);
        w.fillAt(QPoint(150, 150));
        QCOMPARE(pixel(w, 150, 150), QColor(Qt::red));
        QCOMPARE(pixel(w, 50, 50).alpha(), 0);   // stopped by the ring it can see
        QCOMPARE(pixel(w, 100, 100).alpha(), 0); // and the ring's own layer is untouched
    }

    void fillWithColourFillsTheSelectionOrTheLayer()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::red);
        w.setSelection(Selection::ellipse(QRect(10, 10, 60, 60)));
        key(w, Qt::Key_F5, Qt::ShiftModifier);
        QCOMPARE(pixel(w, 40, 40), QColor(Qt::red));
        QCOMPARE(pixel(w, 11, 11), QColor(Qt::white));
        QCOMPARE(w.history().label(0), QStringLiteral("Fill"));
        w.deselect();
        w.fillSelection();
        QCOMPARE(pixel(w, 399, 299), QColor(Qt::red));
        QCOMPARE(pixel(w, 100, 100), QColor(Qt::red));
    }

    void dragDrawsAGradient()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        key(w, Qt::Key_G, Qt::ShiftModifier);
        QCOMPARE(w.canvasView()->tool(), w.gradientTool());
        drag(w, {50, 250}, {350, 250});
        QVERIFY(w.canvasView()->selectionOutline().isEmpty()); // the guide line is gone
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Gradient"));
        // Black fading to nothing over white: dark on the left, white on the right.
        QVERIFY2(pixel(w, 52, 250).red() < 40, qPrintable(pixel(w, 52, 250).name()));
        QVERIFY2(pixel(w, 348, 250).red() > 245, qPrintable(pixel(w, 348, 250).name()));
        const int mid = pixel(w, 200, 20).red();
        QVERIFY2(mid > 90 && mid < 230, qPrintable(QString::number(mid)));
        QCOMPARE(pixel(w, 20, 20), QColor(Qt::black));
        QCOMPARE(pixel(w, 390, 20), QColor(Qt::white));

        w.undo();
        QCOMPARE(pixel(w, 20, 20), QColor(Qt::white));
    }

    void aClickWithTheGradientToolDoesNothing()
    {
        MainWindow w;
        setupWindow(w);
        key(w, Qt::Key_G, Qt::ShiftModifier);
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {150, 150}));
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(pixel(w, 150, 150), QColor(Qt::white));
    }

    void gradientToAnEndColourInsideASelection()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::red);
        MainWindow::GradientOptions o;
        o.toTransparent = false;
        o.end = Qt::blue;
        w.setGradientOptions(o);
        w.setSelection(Selection::rect(QRect(0, 0, 400, 50)));
        w.drawGradient({0, 25}, {400, 25});
        QVERIFY(pixel(w, 2, 25).red() > 245 && pixel(w, 2, 25).blue() < 10);
        QVERIFY(pixel(w, 397, 25).blue() > 245 && pixel(w, 397, 25).red() < 10);
        QCOMPARE(pixel(w, 200, 60), QColor(Qt::white)); // outside the selection
        QVERIFY(!w.canvasView()->selectionOutline().isEmpty()); // still selected

        // Reversed, the ends swap.
        o.reverse = true;
        w.setGradientOptions(o);
        w.drawGradient({0, 25}, {400, 25});
        QVERIFY(pixel(w, 2, 25).blue() > 245);
        QVERIFY(pixel(w, 397, 25).red() > 245);
    }

    void radialGradient()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        MainWindow::GradientOptions o;
        o.radial = true;
        w.setGradientOptions(o);
        w.drawGradient({300, 150}, {340, 150});
        QVERIFY(pixel(w, 300, 150).red() < 50);
        QCOMPARE(pixel(w, 345, 150), QColor(Qt::white));
        QCOMPARE(pixel(w, 300, 195), QColor(Qt::white));
        QCOMPARE(pixel(w, 320, 150), pixel(w, 300, 170));
    }

    void lockedLayerRefuses()
    {
        MainWindow w;
        setupWindow(w);
        w.setLayerLocked(w.layers().activeId(), true);
        const qsizetype steps = w.history().count();
        w.fillAt(QPoint(150, 150));
        w.fillSelection();
        w.drawGradient({0, 0}, {300, 0});
        QCOMPARE(w.history().count(), steps);
        QCOMPARE(pixel(w, 150, 150), QColor(Qt::white));
    }

    void optionsBarsFollowTheTool()
    {
        MainWindow w;
        setupWindow(w);
        const int width = w.width();
        const auto visible = [&](const char *name) {
            return w.findChild<QWidget *>(QString::fromLatin1(name))->isVisible();
        };
        key(w, Qt::Key_G);
        QVERIFY(visible("FillOptionsBar"));
        QVERIFY(!visible("ToolOptionsBar") && !visible("GradientOptionsBar"));
        key(w, Qt::Key_G, Qt::ShiftModifier);
        QVERIFY(visible("GradientOptionsBar"));
        QVERIFY(!visible("FillOptionsBar"));
        key(w, Qt::Key_B);
        QVERIFY(visible("ToolOptionsBar"));
        QVERIFY(!visible("GradientOptionsBar") && !visible("FillOptionsBar") && !visible("TransformOptionsBar"));
        key(w, Qt::Key_W);
        key(w, Qt::Key_G);
        key(w, Qt::Key_B);
        QTest::qWait(50);
        QCOMPARE(w.width(), width); // switching bars never widens the window
    }
};

QTEST_MAIN(TestFillUi)
#include "tst_fillui.moc"
