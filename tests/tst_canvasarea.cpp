#include "canvasarea.h"
#include "canvasview.h"

#include <QScrollBar>
#include <QTest>

using namespace easel;

class TestCanvasArea : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        TileStore store{Qt::white};
        CanvasView *view = new CanvasView;
        CanvasArea area{view};
        Fixture(QSize canvas)
        {
            area.resize(800, 600);
            area.show();
            view->setDocument(&store, canvas);
        }
        double viewW() const { return view->width(); }
        double viewH() const { return view->height(); }
    };

private slots:
    void fitCentresTheBars()
    {
        Fixture f(QSize(4000, 3000));
        QScrollBar *h = f.area.horizontalBar();
        QScrollBar *v = f.area.verticalBar();
        QVERIFY(h->maximum() > 0 && v->maximum() > 0);
        QVERIFY(qAbs(h->value() - h->maximum() / 2) <= 1);
        QVERIFY(qAbs(v->value() - v->maximum() / 2) <= 1);
    }

    void barEndsBringCanvasEdgesToCentre()
    {
        Fixture f(QSize(4000, 3000));
        f.view->setZoomCentered(1.0);
        QScrollBar *h = f.area.horizontalBar();

        h->setValue(h->minimum());
        QVERIFY(qAbs(f.view->canvasViewBounds().left() - f.viewW() / 2) <= 1.0);
        h->setValue(h->maximum());
        QVERIFY(qAbs(f.view->canvasViewBounds().right() - f.viewW() / 2) <= 1.0);

        QScrollBar *v = f.area.verticalBar();
        v->setValue(v->minimum());
        QVERIFY(qAbs(f.view->canvasViewBounds().top() - f.viewH() / 2) <= 1.0);
    }

    void panningMovesTheBars()
    {
        Fixture f(QSize(4000, 3000));
        f.view->setZoomCentered(1.0);
        QScrollBar *h = f.area.horizontalBar();
        const int before = h->value();
        f.view->setPan(f.view->pan() + QPointF(-120, 0)); // content moves left
        QCOMPARE(h->value(), before + 120);                // so the bar moves right
    }

    void zoomingInWidensTheRange()
    {
        Fixture f(QSize(4000, 3000));
        const int fitRange = f.area.horizontalBar()->maximum();
        f.view->setZoomCentered(2.0);
        QVERIFY(f.area.horizontalBar()->maximum() > fitRange * 4);
    }

    void rotationUsesTheRotatedBounds()
    {
        Fixture f(QSize(1000, 1000));
        f.view->setZoomCentered(1.0);
        f.view->rotateBy(45.0);
        // A 1000 px square at 45 degrees is ~1414 px across.
        QVERIFY(qAbs(f.area.horizontalBar()->maximum() - 1414) <= 2);
    }

    void panningPastTheRangeDoesNotJump()
    {
        Fixture f(QSize(1000, 1000));
        f.view->setZoomCentered(1.0);
        const QPointF far = f.view->pan() + QPointF(5000, 0);
        f.view->setPan(far);
        QCOMPARE(f.view->pan(), far); // the bars widened instead of pulling it back
        QCOMPARE(f.area.horizontalBar()->value(), f.area.horizontalBar()->minimum());
    }
};

QTEST_MAIN(TestCanvasArea)
#include "tst_canvasarea.moc"
