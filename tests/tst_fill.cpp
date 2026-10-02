#include "color.h"
#include "fillops.h"
#include "regionops.h"
#include "tilestore.h"

#include <QTest>

using namespace easeletch;

namespace {

QColor colorAt(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

} // namespace

class TestFill : public QObject
{
    Q_OBJECT

private slots:
    void fillsTheRegionAndReportsOnlyChangedTiles()
    {
        TileStore store;
        const QRect canvas(0, 0, 200, 200);
        const auto before = fillRegion(store, Selection::rect(QRect(10, 10, 20, 20)), {}, canvas, Qt::red);
        QCOMPARE(before.size(), 1); // one 64 px tile
        QVERIFY(before.value(TileCoord{0, 0}).isNull()); // it didn't exist before
        QCOMPARE(colorAt(store, 10, 10), QColor(Qt::red));
        QCOMPARE(colorAt(store, 29, 29), QColor(Qt::red));
        QCOMPARE(colorAt(store, 30, 30).alpha(), 0);
        QCOMPARE(colorAt(store, 9, 10).alpha(), 0);

        // The same fill again changes nothing, so there's nothing to undo.
        QVERIFY(fillRegion(store, Selection::rect(QRect(10, 10, 20, 20)), {}, canvas, Qt::red).isEmpty());
    }

    void staysInsideTheClipAndTheCanvas()
    {
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 100, 100);
        fillRegion(store, Selection::rect(QRect(-50, -50, 400, 400)), Selection::ellipse(QRect(20, 20, 40, 40)),
                   canvas, Qt::blue);
        QCOMPARE(colorAt(store, 40, 40), QColor(Qt::blue));
        QCOMPARE(colorAt(store, 21, 21), QColor(Qt::white)); // the ellipse's empty corner
        QCOMPARE(colorAt(store, 80, 80), QColor(Qt::white));
        QVERIFY(!store.hasTile(TileCoord{2, 2})); // nothing written past the canvas
    }

    void aFeatheredRegionFades()
    {
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 100, 100);
        const Selection soft = Selection::rect(QRect(30, 30, 40, 40)).feathered(6, canvas);
        fillRegion(store, soft, {}, canvas, Qt::black);
        QCOMPARE(colorAt(store, 50, 50), QColor(Qt::black));
        const QColor edge = colorAt(store, 30, 50);
        QVERIFY2(edge.red() > 20 && edge.red() < 235, qPrintable(edge.name()));
        QCOMPARE(colorAt(store, 5, 5), QColor(Qt::white));
    }

    void floodFillStopsAtOutlines()
    {
        // A black ring on white: filling inside leaves the outside alone.
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 100, 100);
        store.fillRect(QRect(20, 20, 40, 40), QColor(Qt::black));
        store.fillRect(QRect(22, 22, 36, 36), QColor(Qt::white));
        const Selection inside = magicWand(store, canvas, QPoint(40, 40), 0.1, true);
        fillRegion(store, inside, {}, canvas, Qt::green);
        QCOMPARE(colorAt(store, 40, 40), QColor(Qt::green));
        QCOMPARE(colorAt(store, 22, 22), QColor(Qt::green));
        QCOMPARE(colorAt(store, 21, 21), QColor(Qt::black));
        QCOMPARE(colorAt(store, 5, 5), QColor(Qt::white));
    }

    void gradientRampMixesAsSrgb()
    {
        Gradient g;
        g.start = Qt::black;
        g.end = Qt::white;
        QCOMPARE(pixelToColor(gradientPixel(g, 0.0)), QColor(Qt::black));
        QCOMPARE(pixelToColor(gradientPixel(g, 1.0)), QColor(Qt::white));
        // Halfway is mid grey as other editors show it, not the linear-light middle (188).
        const QColor mid = pixelToColor(gradientPixel(g, 0.5));
        QVERIFY2(std::abs(mid.red() - 128) <= 1, qPrintable(mid.name()));

        // To transparent: the same colour all the way, fading out.
        g.start = Qt::red;
        g.end = Qt::transparent;
        const Pixel half = gradientPixel(g, 0.5);
        QVERIFY(std::abs(float(half.a) - 0.5f) < 0.01f);
        QVERIFY(std::abs(float(half.r) - 0.5f) < 0.01f); // red, premultiplied by a half
        QVERIFY(float(half.g) == 0.0f && float(half.b) == 0.0f);
        QVERIFY(float(gradientPixel(g, 1.0).a) == 0.0f);
    }

    void linearGradientRunsAlongTheLine()
    {
        TileStore store;
        const QRect canvas(0, 0, 200, 100);
        Gradient g;
        g.from = QPointF(50, 50);
        g.to = QPointF(150, 50);
        g.start = Qt::black;
        g.end = Qt::white;
        const auto before = fillGradient(store, {}, canvas, g);
        QCOMPARE(before.size(), 8); // every tile of the canvas (4 x 2)
        QCOMPARE(colorAt(store, 10, 10), QColor(Qt::black)); // before the start: the start colour
        QCOMPARE(colorAt(store, 190, 90), QColor(Qt::white)); // past the end: the end colour
        const QColor mid = colorAt(store, 100, 20);
        QVERIFY2(std::abs(mid.red() - 128) <= 2, qPrintable(mid.name()));
        // Even steps, and the same all the way down a column.
        QVERIFY(colorAt(store, 75, 50).red() < colorAt(store, 100, 50).red());
        QVERIFY(colorAt(store, 100, 50).red() < colorAt(store, 125, 50).red());
        QCOMPARE(colorAt(store, 100, 5), colorAt(store, 100, 95));

        // A diagonal line: equal along its perpendicular.
        TileStore d;
        g.from = QPointF(0, 0);
        g.to = QPointF(100, 100);
        fillGradient(d, {}, canvas, g);
        QCOMPARE(colorAt(d, 20, 60), colorAt(d, 60, 20));
    }

    void radialGradientSpreadsFromTheStart()
    {
        TileStore store;
        const QRect canvas(0, 0, 200, 200);
        Gradient g;
        g.from = QPointF(100, 100);
        g.to = QPointF(150, 100);
        g.start = Qt::white;
        g.end = Qt::black;
        g.radial = true;
        fillGradient(store, {}, canvas, g);
        QVERIFY(colorAt(store, 100, 100).red() > 250);
        QCOMPARE(colorAt(store, 160, 100), QColor(Qt::black));
        QCOMPARE(colorAt(store, 100, 40), QColor(Qt::black));
        // The same distance in any direction is the same colour.
        QCOMPARE(colorAt(store, 125, 100), colorAt(store, 100, 125));
        QCOMPARE(colorAt(store, 125, 100), colorAt(store, 74, 100));
    }

    void gradientOverWhatsThereAndInsideTheSelection()
    {
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 200, 100);
        Gradient g;
        g.from = QPointF(0, 50);
        g.to = QPointF(200, 50);
        g.start = Qt::red;
        g.end = Qt::transparent;
        fillGradient(store, Selection::rect(QRect(0, 0, 200, 50)), canvas, g);
        QCOMPARE(colorAt(store, 100, 80), QColor(Qt::white)); // outside the selection
        const QColor left = colorAt(store, 2, 20), right = colorAt(store, 197, 20);
        // Nearly solid red. Transparency mixes in linear light, so 1% of white shows as about 30 of 255.
        QVERIFY2(left.green() < 40 && left.red() == 255, qPrintable(left.name()));
        QVERIFY2(right.green() > 243, qPrintable(right.name()));                   // nearly untouched white
        QCOMPARE(colorAt(store, 100, 20).alpha(), 255); // laid over the white, not replacing it
    }

    void aZeroLengthGradientDoesNothing()
    {
        TileStore store;
        Gradient g;
        g.from = g.to = QPointF(10, 10);
        QVERIFY(fillGradient(store, {}, QRect(0, 0, 100, 100), g).isEmpty());
        QCOMPARE(store.tileCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestFill)
#include "tst_fill.moc"
