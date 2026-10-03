#include "brushtool.h"
#include "canvasview.h"
#include "color.h"
#include "colorpanel.h"
#include "edittools.h"
#include "fillops.h"
#include "gradienteditor.h"
#include "mainwindow.h"

#include <QComboBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QSpinBox>
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
        o.preset = QStringLiteral("colour-end");
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
        o.shape = GradientShape::Radial;
        w.setGradientOptions(o);
        w.drawGradient({300, 150}, {340, 150});
        QVERIFY(pixel(w, 300, 150).red() < 50);
        QCOMPARE(pixel(w, 345, 150), QColor(Qt::white));
        QCOMPARE(pixel(w, 300, 195), QColor(Qt::white));
        QCOMPARE(pixel(w, 320, 150), pixel(w, 300, 170));
    }

    void metalPresetsAndShapes()
    {
        MainWindow w;
        setupWindow(w);
        MainWindow::GradientOptions o;
        o.preset = QStringLiteral("chrome");
        w.setGradientOptions(o);
        auto *presets = w.findChild<QWidget *>(QStringLiteral("GradientOptionsBar"))->findChildren<QComboBox *>().value(0);
        QVERIFY(presets);
        QCOMPARE(presets->currentText(), QStringLiteral("Chrome"));
        QVERIFY(presets->findData(QStringLiteral("gold")) > 0);
        QVERIFY(!presets->itemIcon(presets->currentIndex()).isNull());

        // A chrome bar: bright at the top, the dark horizon in the middle.
        w.setSelection(Selection::rect(QRect(20, 20, 200, 60)));
        w.drawGradient({0, 20}, {0, 80});
        QVERIFY(pixel(w, 100, 21).lightness() > 230);
        QVERIFY(pixel(w, 100, 49).lightness() < 70);
        QVERIFY(pixel(w, 100, 52).lightness() > 200);
        QCOMPARE(pixel(w, 100, 90), QColor(Qt::white));
        QCOMPARE(w.history().label(0), QStringLiteral("Gradient"));
        // A preset isn't the painting colour: nothing joins Recent.
        QVERIFY(w.colorPanel()->recentColors().isEmpty());

        // A rod: the same preset mirrored about the middle of the bar.
        o.shape = GradientShape::Reflected;
        w.setGradientOptions(o);
        w.drawGradient({0, 50}, {0, 80});
        QCOMPARE(pixel(w, 100, 35), pixel(w, 100, 64));
        QVERIFY(pixel(w, 100, 50).lightness() > 230);

        // Reversed: the dark end first.
        o.shape = GradientShape::Linear;
        o.reverse = true;
        w.setGradientOptions(o);
        QCOMPARE(w.currentGradientStops().first().color, gradientPresets().first().stops.last().color);
    }

    void shadedColourMakesABall()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(QColor(200, 40, 40));
        MainWindow::GradientOptions o;
        o.preset = QStringLiteral("colour-shaded");
        o.shape = GradientShape::Radial;
        w.setGradientOptions(o);
        w.setSelection(Selection::ellipse(QRect(250, 30, 100, 100)));
        w.drawGradient({285, 65}, {345, 110}); // from the highlight, up and to the left, to the far edge
        const QColor highlight = pixel(w, 285, 65), body = pixel(w, 300, 82), shadow = pixel(w, 335, 105);
        QVERIFY2(highlight.lightness() > body.lightness() && body.lightness() > shadow.lightness(),
                 qPrintable(highlight.name() + body.name() + shadow.name()));
        QVERIFY(highlight.lightness() > 215);
        QVERIFY(body.red() > 150 && body.green() < 110); // still the colour picked
        QCOMPARE(pixel(w, 252, 32), QColor(Qt::white));  // outside the ellipse
        // It follows the painting colour.
        w.colorPanel()->setColor(QColor(40, 40, 200));
        QCOMPARE(w.currentGradientStops().at(1).color, QColor(40, 40, 200));
    }

    void conicalSweepsRoundTheStart()
    {
        MainWindow w;
        setupWindow(w);
        MainWindow::GradientOptions o;
        o.preset = QStringLiteral("spun-metal");
        o.shape = GradientShape::Conical;
        w.setGradientOptions(o);
        w.setSelection(Selection::ellipse(QRect(240, 150, 120, 120)));
        w.drawGradient({300, 210}, {360, 210});
        // Light and dark by turns going round, and no seam along the line dragged.
        QVERIFY(std::abs(pixel(w, 340, 209).lightness() - pixel(w, 340, 210).lightness()) <= 3);
        QVERIFY(std::abs(pixel(w, 300, 250).lightness() - pixel(w, 328, 238).lightness()) > 40);
        QCOMPARE(pixel(w, 242, 152), QColor(Qt::white));
    }

    void savedGradients()
    {
        MainWindow w;
        setupWindow(w);
        const GradientStops mine = {{0.0, Qt::red}, {0.4, Qt::yellow}, {1.0, Qt::blue}};
        const QString id = w.saveUserGradient(QStringLiteral("  Mine  "), mine);
        QCOMPARE(id, QStringLiteral("user:Mine"));
        QCOMPARE(w.userGradients().size(), 1);
        MainWindow::GradientOptions o;
        o.preset = id;
        w.setGradientOptions(o);
        QCOMPARE(w.currentGradientStops(), mine);
        auto *bar = w.findChild<QWidget *>(QStringLiteral("GradientOptionsBar"));
        auto *presets = bar->findChildren<QComboBox *>().value(0);
        QCOMPARE(presets->currentText(), QStringLiteral("Mine"));

        // Saving under the same name replaces it.
        w.saveUserGradient(QStringLiteral("Mine"), twoStops(Qt::black, Qt::white));
        QCOMPARE(w.userGradients().size(), 1);
        QCOMPARE(w.currentGradientStops(), twoStops(Qt::black, Qt::white));

        // Removed: the tool falls back to the first gradient.
        QVERIFY(w.removeUserGradient(id));
        QVERIFY(w.userGradients().isEmpty());
        QCOMPARE(w.gradientOptions().preset, QStringLiteral("colour-transparent"));
        QVERIFY(presets->findData(id) < 0);
        QVERIFY(!w.removeUserGradient(QStringLiteral("chrome"))); // built-ins stay

        // A custom gradient (edited, not saved) is in the list once it exists.
        QVERIFY(presets->findData(QStringLiteral("custom")) < 0);
        o = {};
        o.preset = QStringLiteral("custom");
        o.custom = mine;
        w.setGradientOptions(o);
        QCOMPARE(presets->currentText(), QStringLiteral("Custom"));
        QCOMPARE(w.currentGradientStops(), mine);
    }

    void gradientEditorEditsStops()
    {
        GradientDialog dlg(twoStops(Qt::black, Qt::white));
        GradientBar *bar = dlg.bar();
        bar->resize(420, 64);
        QCOMPARE(dlg.stops().size(), 2);

        // A new stop takes the colour the gradient has there.
        const int mid = bar->addStop(0.5);
        QCOMPARE(mid, 1);
        QCOMPARE(bar->selected(), 1);
        QVERIFY(std::abs(dlg.stops().at(1).color.red() - 128) <= 1);
        bar->setStopColor(1, Qt::red);
        QCOMPARE(dlg.stops().at(1).color, QColor(Qt::red));

        // Dragged onto a neighbour it stays on its own side: a hard edge.
        QCOMPARE(bar->setStopPosition(1, 1.0), 1);
        QCOMPARE(dlg.stops().at(1).color, QColor(Qt::red));
        QCOMPARE(dlg.stops().at(2).color, QColor(Qt::white));
        // Moved past a neighbour, it changes places and stays selected.
        bar->setStopPosition(1, 0.5);
        QCOMPARE(bar->addStop(0.25), 1);
        bar->setStopColor(1, Qt::blue);
        QCOMPARE(bar->setStopPosition(1, 0.75), 2);
        QCOMPARE(bar->selected(), 2);
        QCOMPARE(dlg.stops().at(1).color, QColor(Qt::red));
        QCOMPARE(dlg.stops().at(2).color, QColor(Qt::blue));
        QVERIFY(bar->removeStop(2));
        bar->setSelected(1);
        bar->setStopPosition(1, 0.25);
        QCOMPARE(dlg.stops().at(1).position, 0.25);

        // The fields follow the selection; the wheel changes the colour and leaves opacity alone.
        auto *opacity = dlg.findChild<QSpinBox *>();
        QVERIFY(opacity);
        opacity->setValue(40);
        QCOMPARE(dlg.stops().at(1).color.alpha(), 102);
        QCOMPARE(dlg.findChild<QLineEdit *>()->text(), QStringLiteral("#FF0000"));
        bar->setSelected(0);
        QCOMPARE(opacity->value(), 100);
        QCOMPARE(dlg.findChild<QLineEdit *>()->text(), QStringLiteral("#000000"));

        // Mouse: a click on the bar adds a stop; dragging a marker moves it.
        const QRect r = bar->barRect();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(r.left() + r.width() * 3 / 4, r.center().y()));
        QCOMPARE(dlg.stops().size(), 4);
        QVERIFY(std::abs(dlg.stops().at(2).position - 0.75) < 0.01);
        QCOMPARE(bar->selected(), 2);
        const int y = r.bottom() + 8;
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, QPoint(r.left() + r.width() * 3 / 4, y));
        QTest::mouseMove(bar, QPoint(r.left() + r.width() / 2, y));
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, QPoint(r.left() + r.width() / 2, y));
        QCOMPARE(dlg.stops().size(), 4);
        QVERIFY(std::abs(dlg.stops().at(2).position - 0.5) < 0.01);

        // Delete removes the selected stop, down to two and no further.
        QTest::keyClick(bar, Qt::Key_Delete);
        QCOMPARE(dlg.stops().size(), 3);
        QVERIFY(bar->removeStop(1));
        QVERIFY(!bar->removeStop(0));
        QCOMPARE(dlg.stops().size(), 2);
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
