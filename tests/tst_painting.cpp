#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "eyedroppertool.h"
#include "mainwindow.h"

#include <QPointingDevice>
#include <QTabletEvent>
#include <QTest>

using namespace easel;

namespace {

QColor pixel(MainWindow &w, int x, int y)
{
    const QColor c = pixelToColor(w.layer()->pixel(x, y));
    return QColor(c.red(), c.green(), c.blue(), c.alpha()); // compare at 8 bits
}

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
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
    b.pressureSize = true;
    b.pressureOpacity = false;
    w.brushTool()->setMode(BrushMode::Paint);
    w.brushTool()->setSettings(b);
    w.brushTool()->setColor(Qt::red);
}

void sendTablet(QWidget *target, const QPointingDevice *pen, QEvent::Type type, QPointF pos,
                double pressure)
{
    const Qt::MouseButton button = type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons buttons = type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton;
    QTabletEvent e(type, pen, pos, target->mapToGlobal(pos), pressure, 0, 0, 0, 0, 0,
                   Qt::NoModifier, button, buttons);
    QApplication::sendEvent(target, &e);
}

} // namespace

class TestPainting : public QObject
{
    Q_OBJECT

private slots:
    void mouseStrokePaintsAndUndoes()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();

        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 100}));
        for (int x = 60; x <= 300; x += 10)
            QTest::mouseMove(view, viewPos(w, {double(x), 100}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {300, 100}));

        QCOMPARE(pixel(w, 50, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 175, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 175, 120), QColor(Qt::white));
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Brush"));

        w.undo();
        QCOMPARE(pixel(w, 175, 100), QColor(Qt::white));
        QCOMPARE(w.layer()->tileCount(), 0);
        w.redo();
        QCOMPARE(pixel(w, 175, 100), QColor(Qt::red));
    }

    void penPressureControlsSize()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        QPointingDevice pen(QStringLiteral("test pen"), 42, QInputDevice::DeviceType::Stylus,
                            QPointingDevice::PointerType::Pen,
                            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                            1, 3);

        // Light pressure: 20 px brush at 25% is 5 px wide.
        sendTablet(view, &pen, QEvent::TabletPress, viewPos(w, {100, 50}), 0.25);
        sendTablet(view, &pen, QEvent::TabletRelease, viewPos(w, {100, 50}), 0.0);
        QCOMPARE(pixel(w, 100, 50), QColor(Qt::red));
        QCOMPARE(pixel(w, 100, 55), QColor(Qt::white));

        // Full pressure: 20 px wide.
        sendTablet(view, &pen, QEvent::TabletPress, viewPos(w, {200, 50}), 1.0);
        sendTablet(view, &pen, QEvent::TabletRelease, viewPos(w, {200, 50}), 0.0);
        QCOMPARE(pixel(w, 200, 55), QColor(Qt::red));

        QCOMPARE(w.history().count(), 2);
    }

    void eraserAndBrushSizeShortcuts()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();

        // Shortcuts fire in the active window, as when the user clicks into it.
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        view->setFocus();

        QTest::keyClick(view, Qt::Key_E);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Erase);
        const double before = w.brushTool()->settings().size;
        QTest::keyClick(view, Qt::Key_BracketRight);
        QVERIFY(w.brushTool()->settings().size > before);

        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {150, 150}));
        QCOMPARE(pixelToColor(w.layer()->pixel(150, 150)).alpha(), 0); // erased to transparent
        QCOMPARE(w.history().label(0), QStringLiteral("Eraser"));

        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Paint);
    }

    void eyedropperPicksWithoutHistory()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        w.layer()->fillRect(QRect(0, 0, 100, 100), QColor(20, 140, 60));
        view->refresh();

        // Alt-click with the brush picks, then painting uses the picked colour.
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, viewPos(w, {50, 50}));
        QCOMPARE(w.colorPanel()->color(), QColor(20, 140, 60));
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(view->tool(), static_cast<CanvasTool *>(w.brushTool())); // still the brush

        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {300, 200}));
        QCOMPARE(pixel(w, 300, 200), QColor(20, 140, 60));
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.colorPanel()->recentColors().value(0), QColor(20, 140, 60));

        // The I tool: dragging picks continuously; outside the canvas is ignored.
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_I);
        QCOMPARE(view->tool(), static_cast<CanvasTool *>(w.eyedropperTool()));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {150, 150}));
        QCOMPARE(w.colorPanel()->color(), QColor(Qt::white));
        QTest::mouseMove(view, viewPos(w, {50, 50}));
        QCOMPARE(w.colorPanel()->color(), QColor(20, 140, 60));
        QTest::mouseMove(view, viewPos(w, {-40, -40}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {-40, -40}));
        QCOMPARE(w.colorPanel()->color(), QColor(20, 140, 60));
        QCOMPARE(w.history().count(), 1);

        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(view->tool(), static_cast<CanvasTool *>(w.brushTool()));
    }

    void spaceDragPansInsteadOfPainting()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        view->setFocus();
        const QPointF panBefore = view->pan();

        QTest::keyPress(view, Qt::Key_Space);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 100}));
        QTest::mouseMove(view, viewPos(w, {100, 100}) + QPoint(40, 0));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 100}) + QPoint(40, 0));
        QTest::keyRelease(view, Qt::Key_Space);

        QVERIFY(view->pan() != panBefore);
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(w.layer()->tileCount(), 0);
    }
};

QTEST_MAIN(TestPainting)
#include "tst_painting.moc"
