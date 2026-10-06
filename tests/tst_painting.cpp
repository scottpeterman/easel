#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "mainwindow.h"

#include <QComboBox>
#include <QPointingDevice>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTest>

using namespace easeletch;

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
    void cloneToolCopiesFromWhereAltWasClicked()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        w.layer()->fillRect(QRect(50, 90, 20, 20), QColor(Qt::blue));
        view->refresh();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_C);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Clone);
        QCOMPARE(view->tool(), w.brushTool());
        BrushSettings b = w.brushTool()->settings();
        b.size = 30;
        b.hardness = 1.0;
        w.brushTool()->setSettings(b);

        // Before a source is chosen, a stroke does nothing.
        const qsizetype steps = w.history().count();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 200}));
        QCOMPARE(w.history().count(), steps);
        QVERIFY(!w.brushTool()->hasCloneSource());

        // Alt+click sets the source (rather than picking a colour), and shows a marker there.
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, viewPos(w, {60, 100}));
        QVERIFY(w.brushTool()->hasCloneSource());
        QCOMPARE(w.history().count(), steps);
        QCOMPARE(w.brushTool()->color(), QColor(Qt::red)); // the painting colour is as it was
        QCOMPARE(view->handles().size(), 1);

        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 200}));
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Clone"));
        QCOMPARE(pixel(w, 200, 200), QColor(Qt::blue));
        QCOMPARE(pixel(w, 205, 205), QColor(Qt::blue));
        QCOMPARE(pixel(w, 213, 200), QColor(Qt::white)); // beside the square, the white beside it
        // A second stroke carries on the same copy: 100 px along from the
        // first, it copies from 100 px along from the source.
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {300, 200}));
        QCOMPARE(pixel(w, 300, 200), QColor(Qt::white));
        w.undo();
        w.undo();
        QCOMPARE(pixel(w, 200, 200), QColor(Qt::white));

        // Another tool: the marker goes, and Alt picks colours again.
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(view->handles().isEmpty());
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, viewPos(w, {60, 100}));
        QCOMPARE(w.brushTool()->color(), QColor(Qt::blue));
    }

    void healToolRemovesABlemish()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        w.layer()->fillRect(QRect(195, 145, 10, 10), QColor(Qt::black));
        view->refresh();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_H);
        QCOMPARE(w.brushTool()->mode(), BrushMode::Heal);
        const qsizetype steps = w.history().count();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 150}));
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Heal"));
        for (int y = 140; y < 160; ++y)
            for (int x = 190; x < 210; ++x)
                QCOMPARE(pixel(w, x, y), QColor(Qt::white));
        w.undo();
        QCOMPARE(pixel(w, 200, 150), QColor(Qt::black));
    }

    void shiftClickDrawsAStraightLineFromTheLastStroke()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        // Nothing to draw from yet: Shift+click is a click.
        QVERIFY(!w.brushTool()->lastStrokeEnd(nullptr));
        QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {50, 50}));
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(pixel(w, 50, 50), QColor(Qt::red));
        QPointF end;
        QVERIFY(w.brushTool()->lastStrokeEnd(&end));
        QVERIFY(qAbs(end.x() - 50) <= 1 && qAbs(end.y() - 50) <= 1);

        // From there to the next Shift+click, in one step.
        QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {300, 200}));
        QCOMPARE(w.history().count(), 2);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Brush"));
        QCOMPARE(pixel(w, 175, 125), QColor(Qt::red));
        QCOMPARE(pixel(w, 100, 80), QColor(Qt::red));
        QCOMPARE(pixel(w, 300, 200), QColor(Qt::red));
        QCOMPARE(pixel(w, 175, 100), QColor(Qt::white)); // straight: nothing off the line
        QCOMPARE(pixel(w, 175, 150), QColor(Qt::white));

        // Dragged on after the press, the stroke carries on from the line's end.
        QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {300, 100}));
        for (int x = 310; x <= 380; x += 10)
            QTest::mouseMove(view, viewPos(w, {double(x), 100}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {380, 100}));
        QCOMPARE(w.history().count(), 3);
        QCOMPARE(pixel(w, 300, 150), QColor(Qt::red));
        QCOMPARE(pixel(w, 350, 100), QColor(Qt::red));

        // Without Shift, a click is a dot on its own.
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {60, 250}));
        QCOMPARE(pixel(w, 60, 250), QColor(Qt::red));
        QCOMPARE(pixel(w, 220, 175), QColor(Qt::white)); // half-way back to the last stroke's end
        // Undo takes back the whole line.
        w.undo();
        w.undo();
        w.undo();
        QCOMPARE(pixel(w, 175, 125), QColor(Qt::white));
        QCOMPARE(pixel(w, 50, 50), QColor(Qt::red));

        // The eraser draws its lines the same way.
        w.brushTool()->setMode(BrushMode::Erase);
        BrushSettings b = w.brushTool()->settings();
        b.size = 20;
        b.hardness = 1.0;
        w.brushTool()->setSettings(b);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 200}));
        QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {350, 200}));
        QCOMPARE(pixel(w, 200, 200).alpha(), 0);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Eraser"));
        w.brushTool()->setMode(BrushMode::Paint);

        // Another canvas: there's nowhere to draw from again.
        w.newDocument(QSize(200, 200), Qt::white);
        QVERIFY(!w.brushTool()->lastStrokeEnd(nullptr));
    }

    void mirrorPaintsBothSidesAndShowsItsLine()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        BrushTool *brush = w.brushTool();
        auto *mirror = w.findChild<QComboBox *>(QStringLiteral("brushMirror"));
        auto *mirrorX = w.findChild<QSpinBox *>(QStringLiteral("brushMirrorX"));
        auto *mirrorY = w.findChild<QSpinBox *>(QStringLiteral("brushMirrorY"));
        auto *centre = w.findChild<QPushButton *>(QStringLiteral("brushMirrorCentre"));
        QVERIFY(mirror && mirrorX && mirrorY && centre);
        QCOMPARE(brush->symmetry(), Symmetry::Off);
        QVERIFY(view->guides().isEmpty());

        // Left / right: a line down the middle of the canvas, and every stroke twice.
        mirror->setCurrentIndex(1);
        emit mirror->activated(1);
        QCOMPARE(brush->symmetry(), Symmetry::LeftRight);
        QCOMPARE(view->guides(), QList<QLineF>({QLineF(200, 0, 200, 300)}));
        QCOMPARE(mirrorX->value(), 200);
        QVERIFY(mirrorX->isEnabled() && !mirrorY->isEnabled() && !centre->isEnabled());
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {60, 100}));
        QTest::mouseMove(view, viewPos(w, {90, 100}));
        QTest::mouseMove(view, viewPos(w, {120, 100}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {120, 100}));
        QCOMPARE(pixel(w, 90, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 309, 100), QColor(Qt::red));
        QCOMPARE(pixel(w, 200, 100), QColor(Qt::white));
        for (int y = 85; y <= 115; ++y)
            for (int x = 40; x <= 140; ++x)
                QCOMPARE(pixel(w, x, y), pixel(w, 399 - x, y));
        // One stroke, one step: undo clears both sides.
        QCOMPARE(w.history().count(), 1);
        w.undo();
        QCOMPARE(pixel(w, 90, 100), QColor(Qt::white));
        QCOMPARE(pixel(w, 309, 100), QColor(Qt::white));

        // The line goes where it's put, and back to the middle.
        mirrorX->setValue(100);
        QVERIFY(!brush->symmetryAxisCentred());
        QCOMPARE(view->guides(), QList<QLineF>({QLineF(100, 0, 100, 300)}));
        QVERIFY(centre->isEnabled());
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {60, 200}));
        QCOMPARE(pixel(w, 60, 200), QColor(Qt::red));
        QCOMPARE(pixel(w, 139, 200), QColor(Qt::red));
        QCOMPARE(pixel(w, 339, 200), QColor(Qt::white));
        centre->click();
        QVERIFY(brush->symmetryAxisCentred());
        QCOMPARE(mirrorX->value(), 200);

        // Both: two lines, four strokes. A Shift+click line is mirrored like any other.
        brush->setSymmetry(Symmetry::Quarters);
        QCOMPARE(mirror->currentIndex(), 3);
        QCOMPARE(view->guides().size(), 2);
        QVERIFY(mirrorY->isEnabled());
        QCOMPARE(mirrorY->value(), 150);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {30, 30}));
        QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {150, 30}));
        for (const QPoint p : {QPoint(90, 30), QPoint(309, 30), QPoint(90, 269), QPoint(309, 269)})
            QCOMPARE(pixel(w, p.x(), p.y()), QColor(Qt::red));

        // It's the brush's and the eraser's: no line for the others, or for another tool.
        brush->setMode(BrushMode::Smudge);
        QVERIFY(view->guides().isEmpty());
        QVERIFY(!mirror->isEnabled());
        brush->setMode(BrushMode::Erase);
        QCOMPARE(view->guides().size(), 2);
        QVERIFY(mirror->isEnabled());
        brush->setMode(BrushMode::Paint);
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_U);
        QVERIFY(view->guides().isEmpty());
        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(view->guides().size(), 2);

        // A centred line follows the canvas to the next one.
        w.newDocument(QSize(300, 200), Qt::white);
        QCOMPARE(view->guides(), QList<QLineF>({QLineF(150, 0, 150, 200), QLineF(0, 100, 300, 100)}));
        brush->setSymmetry(Symmetry::Off);
        QVERIFY(view->guides().isEmpty());
    }

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

    void newWindowShowsTheWholeCanvas()
    {
        MainWindow w; // its 2000 x 1500 document is created before the window has a size
        w.resize(1300, 850);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        CanvasView *view = w.canvasView();
        const QRectF b = view->canvasViewBounds();
        QVERIFY2(b.left() >= 0 && b.top() >= 0 && b.right() <= view->width() && b.bottom() <= view->height(),
                 qPrintable(QStringLiteral("canvas %1,%2 %3x%4 in view %5x%6")
                                .arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height())
                                .arg(view->width()).arg(view->height())));
    }

    void saveReopenAndUnsavedState()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("drawing.easeletch"));
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        QVERIFY(!w.isModified());

        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 100}));
        QVERIFY(w.isModified());
        QVERIFY(w.isWindowModified());

        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(!w.isModified());
        QCOMPARE(w.documentPath(), QFileInfo(path).absoluteFilePath());
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("drawing.easeletch")));

        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 100}));
        QVERIFY(w.isModified());
        w.undo();
        QVERIFY(!w.isModified()); // back at the saved state
        w.undo();
        QVERIFY(w.isModified());
        w.redo();

        // Reopen in a fresh window: same pixels, saves back to the same file.
        MainWindow w2;
        setupWindow(w2);
        QSignalSpy opened(&w2, &MainWindow::documentOpened);
        w2.openDocument(path);
        QVERIFY(opened.wait(10000));
        QCOMPARE(opened.at(0).at(1).toBool(), true);
        QCOMPARE(w2.canvasView()->canvasSize(), QSize(400, 300));
        QCOMPARE(pixel(w2, 100, 100), QColor(Qt::red));
        QCOMPARE(pixel(w2, 200, 100), QColor(Qt::white));
        QCOMPARE(w2.documentPath(), QFileInfo(path).absoluteFilePath());
        QVERIFY(!w2.isModified());
    }

    void backgroundSaveLetsYouKeepPainting()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bg.easeletch"));
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 100}));

        QSignalSpy saved(&w, &MainWindow::documentSaved);
        QVERIFY(w.saveDocumentTo(path)); // background
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {300, 100})); // meanwhile
        QVERIFY(saved.wait(10000));
        QCOMPARE(saved.at(0).at(1).toBool(), true);
        QVERIFY(w.isModified()); // the second stroke isn't in the file

        const LoadedDocument doc = loadDocument(path);
        QVERIFY(doc.ok());
        QCOMPARE(pixelToColor(doc.stack->layers().first().store.pixel(100, 100)), QColor(Qt::red));
        QCOMPARE(pixelToColor(doc.stack->layers().first().store.pixel(300, 100)), QColor(Qt::white));
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
