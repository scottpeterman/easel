#include "adjustpanel.h"
#include "brushtool.h"
#include "canvasview.h"
#include "color.h"
#include "colorpanel.h"
#include "layerpanel.h"
#include "mainwindow.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace easeletch;

namespace {

QColor shown(MainWindow &w, int x, int y)
{
    w.canvasView()->refresh(); // brings the composite up to date
    const QColor c = pixelToColor(w.layers().composite().pixel(x, y));
    return QColor(c.red(), c.green(), c.blue(), c.alpha());
}

bool nearly(const QColor &a, const QColor &b, int tolerance = 2)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance;
}

// 400 x 300 white canvas at 100%, a red 20 x 20 square at (50, 90).
void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    w.layer()->fillRect(QRect(50, 90, 20, 20), QColor(Qt::red));
    w.canvasView()->refresh();
    w.canvasView()->setFocus();
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

} // namespace

class TestAdjustUi : public QObject
{
    Q_OBJECT

private slots:
    void addEditUndo()
    {
        MainWindow w;
        setupWindow(w);
        const int background = w.layers().activeId();
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::HueSaturation));
        const int adj = w.layers().activeId();
        QVERIFY(adj != background);
        QVERIFY(w.layers().active()->isAdjustment());
        QCOMPARE(w.layers().active()->name, QStringLiteral("Hue / Saturation 1"));
        QCOMPARE(w.layers().layers().last().id, adj); // above the layer that was active
        QCOMPARE(w.history().label(0), QStringLiteral("New Hue / Saturation Layer"));
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::HueSaturation);
        QCOMPARE(shown(w, 60, 100), QColor(Qt::red)); // nothing changed yet

        // The panel's sliders change it live; a run of changes is one undo step.
        auto *hue = w.adjustPanel()->findChildren<QDoubleSpinBox *>().value(0);
        for (QDoubleSpinBox *s : w.adjustPanel()->findChildren<QDoubleSpinBox *>())
            if (s->isVisible() && s->suffix() == QStringLiteral("°"))
                hue = s;
        QVERIFY(hue);
        hue->setValue(60.0);
        hue->setValue(120.0);
        QCOMPARE(w.layers().active()->adjust.hue, 120.0);
        QVERIFY(nearly(shown(w, 60, 100), QColor(0, 255, 0)));
        QCOMPARE(shown(w, 5, 5), QColor(Qt::white));
        QCOMPARE(w.history().count(), 2);
        QCOMPARE(w.history().label(1), QStringLiteral("Change Hue / Saturation"));
        // The layer below is untouched: that's the point.
        QCOMPARE(pixelToColor(w.layers().layer(background)->store.pixel(60, 100)), QColor(Qt::red));

        w.undo();
        QCOMPARE(w.layers().active()->adjust.hue, 0.0);
        QCOMPARE(hue->value(), 0.0); // the panel follows
        QCOMPARE(shown(w, 60, 100), QColor(Qt::red));
        w.redo();
        QVERIFY(nearly(shown(w, 60, 100), QColor(0, 255, 0)));
        QCOMPARE(hue->value(), 120.0);

        // Reset goes back to no change, as a step of its own after other work.
        w.setLayerOpacity(adj, 0.5);
        const qsizetype steps = w.history().count();
        for (QToolButton *b : w.adjustPanel()->findChildren<QToolButton *>())
            if (b->isVisible() && b->text() == QStringLiteral("Reset"))
                b->click();
        QCOMPARE(w.layers().active()->adjust.hue, 0.0);
        QCOMPARE(w.history().count(), steps + 1);
    }

    void noPaintingOnAnAdjustmentLayer()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::Levels));
        QVERIFY(w.layer() == nullptr);
        const qsizetype steps = w.history().count();
        drag(w, {100, 100}, {200, 150});
        w.fillSelection();
        QCOMPARE(w.history().count(), steps);
        QCOMPARE(w.layers().active()->store.tileCount(), 0);
        QVERIFY(!w.beginTransform());

        // Its mask can be painted: black hides the adjustment there.
        Adjustment a = w.layers().active()->adjust;
        a.outWhite = 0.0; // everything black
        w.setAdjustment(w.layers().activeId(), a);
        QCOMPARE(shown(w, 300, 200), QColor(Qt::black));
        w.setSelection(Selection::rect(QRect(200, 100, 100, 100)));
        w.invertSelection();
        QVERIFY(w.addLayerMask()); // shows only the selection: everywhere but that square
        QVERIFY(w.isEditingMask());
        QCOMPARE(shown(w, 250, 150), QColor(Qt::white)); // masked out: unadjusted
        QCOMPARE(shown(w, 20, 20), QColor(Qt::black));
        QVERIFY(!w.applyLayerMask());
    }

    void curvesByDraggingAPoint()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::Curves));
        CurveWidget *curve = w.adjustPanel()->curveWidget();
        QVERIFY(curve && curve->isVisible());
        QCOMPARE(curve->points().size(), 2);
        const QRectF r = curve->plotRect();
        const auto at = [&](double x, double y) { return QPointF(r.left() + x * r.width(), r.bottom() - y * r.height()); };

        // A press on the line adds a point; dragging it up lifts the midtones.
        const qsizetype steps = w.history().count();
        sendMouse(curve, QEvent::MouseButtonPress, at(0.5, 0.5), Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 5; ++i)
            sendMouse(curve, QEvent::MouseMove, at(0.5, 0.5 + 0.05 * i), Qt::NoButton, Qt::LeftButton);
        sendMouse(curve, QEvent::MouseButtonRelease, at(0.5, 0.75), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(curve->points().size(), 3);
        QVERIFY(std::abs(curve->points().at(1).y() - 0.75) < 0.02);
        QCOMPARE(w.layers().active()->adjust.curve, curve->points());
        QCOMPARE(w.history().count(), steps + 1); // the whole drag is one step

        w.layers(); // (composite checked through what's shown)
        MainWindow &win = w;
        win.setActiveLayer(win.layers().layers().first().id);
        win.layer()->fillRect(QRect(200, 200, 20, 20), QColor(128, 128, 128));
        QVERIFY(nearly(shown(w, 210, 210), QColor(191, 191, 191), 5));
        QCOMPARE(shown(w, 5, 5), QColor(Qt::white));

        // Dragged off the square, the point is removed and the curve is straight again.
        win.setActiveLayer(win.layers().layers().last().id);
        sendMouse(curve, QEvent::MouseButtonPress, at(0.5, 0.75), Qt::LeftButton, Qt::LeftButton);
        sendMouse(curve, QEvent::MouseMove, QPointF(r.right() + 80, r.top() - 80), Qt::NoButton, Qt::LeftButton);
        sendMouse(curve, QEvent::MouseButtonRelease, QPointF(r.right() + 80, r.top() - 80), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(curve->points().size(), 2);
        QVERIFY(nearly(shown(w, 210, 210), QColor(128, 128, 128)));
    }

    void panelFollowsTheActiveLayer()
    {
        MainWindow w;
        setupWindow(w);
        const int background = w.layers().activeId();
        auto *adjustDock = w.findChild<QDockWidget *>(QStringLiteral("AdjustmentDock"));
        auto *colorDock = w.findChild<QDockWidget *>(QStringLiteral("ColorDock"));
        QVERIFY(adjustDock && colorDock);
        const auto front = [](QDockWidget *d) { return !d->visibleRegion().isEmpty(); };
        QTRY_VERIFY(front(colorDock));
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::None);

        // With no adjustment layer active, the panel offers to add one.
        colorDock->setFloating(true); // both in view, so the button can be clicked
        auto *add = w.adjustPanel()->findChild<QToolButton *>(QStringLiteral("add-exposure"));
        QVERIFY(add);
        add->click();
        QVERIFY(w.layers().active()->isAdjustment());
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::Exposure);
        w.setActiveLayer(background);
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::None);
        QVERIFY(w.layerPanel()->tree()->currentItem() != nullptr);
    }

    void thresholdLayerTurnsThePictureBlackAndWhite()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::Threshold));
        QCOMPARE(w.adjustPanel()->adjustment().type, AdjustmentType::Threshold);
        QVERIFY(w.adjustPanel()->findChild<QWidget *>(QStringLiteral("add-threshold")) != nullptr);
        // Red looks darker than half way: black. The white around it stays white.
        QCOMPARE(shown(w, 60, 100).rgba(), QColor(Qt::black).rgba());
        QCOMPARE(shown(w, 200, 200).rgba(), QColor(Qt::white).rgba());
        // Lower the level under red's lightness and it turns white.
        Adjustment a = w.adjustPanel()->adjustment();
        a.threshold = 0.2;
        w.setAdjustment(w.layers().activeId(), a);
        QCOMPARE(shown(w, 60, 100).rgba(), QColor(Qt::white).rgba());
    }

    void mergeDownAndSaveReopen()
    {
        MainWindow w;
        setupWindow(w);
        const int background = w.layers().activeId();
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::BlackWhite));
        QVERIFY(nearly(shown(w, 60, 100), QColor(77, 77, 77)));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bw.easeletch"));
        QVERIFY(w.saveDocumentTo(path, true));
        MainWindow again;
        again.show();
        QSignalSpy opened(&again, &MainWindow::documentOpened);
        again.openDocument(path);
        QVERIFY(opened.wait(10000));
        QCOMPARE(again.layers().count(), 2);
        QVERIFY(again.layers().layers().last().isAdjustment());
        QVERIFY(nearly(shown(again, 60, 100), QColor(77, 77, 77)));

        // Merge Down makes it permanent on the layer below.
        QVERIFY(w.mergeDown());
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.layers().activeId(), background);
        QVERIFY(nearly(pixelToColor(w.layer()->pixel(60, 100)), QColor(77, 77, 77)));
        w.undo();
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(pixelToColor(w.layers().layer(background)->store.pixel(60, 100)), QColor(Qt::red));
    }

    void curvesOnTwentyLayersKeepsUpWithADrag()
    {
        // The milestone's measure: 2000 x 1500, twenty painted layers, Curves on top.
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newDocument(QSize(2000, 1500), Qt::white);
        for (int i = 0; i < 19; ++i) {
            QVERIFY(w.addLayer());
            // Each covers most of the canvas, partly see-through.
            w.layer()->fillRect(QRect(40 * i, 30 * i, 1200, 900), QColor(10 * i, 255 - 10 * i, 128, 90));
        }
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::Curves));
        const int adj = w.layers().activeId();
        w.canvasView()->refresh();

        QElapsedTimer timer;
        qint64 worst = 0, total = 0;
        const int moves = 10;
        for (int i = 1; i <= moves; ++i) {
            Adjustment a = w.layers().active()->adjust;
            a.curve = {{0.0, 0.0}, {0.5, 0.5 + 0.03 * i}, {1.0, 1.0}};
            timer.start();
            w.setAdjustment(adj, a);
            w.canvasView()->refresh(); // what the drag's redraw does
            const qint64 ms = timer.elapsed();
            worst = std::max(worst, ms);
            total += ms;
        }
        qInfo("Curves over 20 layers at 2000 x 1500: %lld ms average, %lld ms worst per move", total / moves, worst);
        QVERIFY2(total / moves < 400, "too slow to follow a drag");
        QVERIFY(shown(w, 1000, 700).green() != 0);
    }
};

QTEST_MAIN(TestAdjustUi)
#include "tst_adjustui.moc"
