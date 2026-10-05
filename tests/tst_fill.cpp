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
        g.stops = twoStops(Qt::black, Qt::white);
        QCOMPARE(pixelToColor(gradientPixel(g.stops, 0.0)), QColor(Qt::black));
        QCOMPARE(pixelToColor(gradientPixel(g.stops, 1.0)), QColor(Qt::white));
        // Halfway is mid grey as other editors show it, not the linear-light middle (188).
        const QColor mid = pixelToColor(gradientPixel(g.stops, 0.5));
        QVERIFY2(std::abs(mid.red() - 128) <= 1, qPrintable(mid.name()));

        // To transparent: the same colour all the way, fading out.
        g.stops = twoStops(Qt::red, Qt::transparent);
        const Pixel half = gradientPixel(g.stops, 0.5);
        QVERIFY(std::abs(float(half.a) - 0.5f) < 0.01f);
        QVERIFY(std::abs(float(half.r) - 0.5f) < 0.01f); // red, premultiplied by a half
        QVERIFY(float(half.g) == 0.0f && float(half.b) == 0.0f);
        QVERIFY(float(gradientPixel(g.stops, 1.0).a) == 0.0f);
    }

    void linearGradientRunsAlongTheLine()
    {
        TileStore store;
        const QRect canvas(0, 0, 200, 100);
        Gradient g;
        g.from = QPointF(50, 50);
        g.to = QPointF(150, 50);
        g.stops = twoStops(Qt::black, Qt::white);
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
        g.stops = twoStops(Qt::white, Qt::black);
        g.shape = GradientShape::Radial;
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
        g.stops = twoStops(Qt::red, Qt::transparent);
        fillGradient(store, Selection::rect(QRect(0, 0, 200, 50)), canvas, g);
        QCOMPARE(colorAt(store, 100, 80), QColor(Qt::white)); // outside the selection
        const QColor left = colorAt(store, 2, 20), right = colorAt(store, 197, 20);
        // Nearly solid red. Transparency mixes in linear light, so 1% of white shows as about 30 of 255.
        QVERIFY2(left.green() < 40 && left.red() == 255, qPrintable(left.name()));
        QVERIFY2(right.green() > 243, qPrintable(right.name()));                   // nearly untouched white
        QCOMPARE(colorAt(store, 100, 20).alpha(), 255); // laid over the white, not replacing it
    }

    void stopsMixBetweenNeighboursOnly()
    {
        const GradientStops stops = {{0.0, Qt::red}, {0.5, Qt::green}, {1.0, Qt::blue}};
        QCOMPARE(pixelToColor(gradientPixel(stops, 0.0)), QColor(Qt::red));
        QCOMPARE(pixelToColor(gradientPixel(stops, 0.5)), QColor(Qt::green));
        QCOMPARE(pixelToColor(gradientPixel(stops, 1.0)), QColor(Qt::blue));
        const QColor a = pixelToColor(gradientPixel(stops, 0.25));
        QVERIFY2(std::abs(a.red() - 128) <= 1 && std::abs(a.green() - 128) <= 1 && a.blue() == 0, qPrintable(a.name()));
        const QColor b = pixelToColor(gradientPixel(stops, 0.75));
        QVERIFY2(b.red() == 0 && std::abs(b.green() - 128) <= 1 && std::abs(b.blue() - 128) <= 1, qPrintable(b.name()));
        // Before the first stop and after the last, their colours hold.
        const GradientStops inner = {{0.2, Qt::red}, {0.8, Qt::blue}};
        QCOMPARE(pixelToColor(gradientPixel(inner, 0.1)), QColor(Qt::red));
        QCOMPARE(pixelToColor(gradientPixel(inner, 0.95)), QColor(Qt::blue));
    }

    void twoStopsAtOnePlaceMakeAHardEdge()
    {
        const GradientStops stops = {{0.0, Qt::black}, {0.5, Qt::black}, {0.5, Qt::white}, {1.0, Qt::white}};
        QCOMPARE(pixelToColor(gradientPixel(stops, 0.499)), QColor(Qt::black));
        QCOMPARE(pixelToColor(gradientPixel(stops, 0.5)), QColor(Qt::white));
    }

    void aTransparentStopTakesItsNeighboursColour()
    {
        // Red, clear in the middle, blue: no black or white creeps in as it fades.
        const GradientStops stops = {{0.0, Qt::red}, {0.5, Qt::transparent}, {1.0, Qt::blue}};
        const Pixel left = gradientPixel(stops, 0.25), right = gradientPixel(stops, 0.75);
        QVERIFY(std::abs(float(left.a) - 0.5f) < 0.01f && float(left.g) == 0.0f && float(left.b) == 0.0f);
        QVERIFY(std::abs(float(right.a) - 0.5f) < 0.01f && float(right.r) == 0.0f && float(right.g) == 0.0f);
    }

    void stopsNormalizeReverseAndSurviveText()
    {
        GradientStops messy = {{1.4, Qt::blue}, {0.5, Qt::green}, {-0.2, Qt::red}, {0.7, QColor()}};
        messy = normalizedStops(messy);
        QCOMPARE(messy.size(), 3);
        QCOMPARE(messy.at(0).position, 0.0);
        QCOMPARE(messy.at(0).color, QColor(Qt::red));
        QCOMPARE(messy.at(2).position, 1.0);
        QCOMPARE(normalizedStops({}).size(), 2);
        QCOMPARE(normalizedStops({{0.3, Qt::red}}).size(), 2);

        const GradientStops back = reversedStops({{0.0, Qt::red}, {0.25, Qt::green}, {1.0, Qt::blue}});
        QCOMPARE(back.at(0).color, QColor(Qt::blue));
        QCOMPARE(back.at(1).position, 0.75);
        QCOMPARE(back.at(2).color, QColor(Qt::red));

        const GradientStops stops = {{0.0, QColor(255, 0, 0, 128)}, {0.375, QColor("#12ab34")}, {1.0, Qt::transparent}};
        QCOMPARE(stopsFromString(stopsToString(stops)), stops);
        QVERIFY(stopsFromString(QStringLiteral("nonsense")).isEmpty());
        QVERIFY(stopsFromString(QString()).isEmpty());
    }

    void presetsAreWellFormed()
    {
        QStringList ids;
        for (const GradientPreset &p : gradientPresets()) {
            QVERIFY2(p.stops.size() >= 2, qPrintable(p.id));
            QVERIFY(!p.name.isEmpty());
            QVERIFY(!ids.contains(p.id));
            ids << p.id;
            QCOMPARE(p.stops, normalizedStops(p.stops)); // in order, in range, valid colours
            QCOMPARE(p.stops.first().position, 0.0);
            QCOMPARE(p.stops.last().position, 1.0);
        }
        QVERIFY(ids.contains(QStringLiteral("chrome")) && ids.contains(QStringLiteral("gold")));
        // The armours: dark at both ends, with a highlight in between that is
        // both much lighter and a different hue from the body colour.
        for (const char *id : {"amber-armour", "teal-armour", "magenta-armour"}) {
            QVERIFY2(ids.contains(QLatin1String(id)), id);
            for (const GradientPreset &p : gradientPresets()) {
                if (p.id != QLatin1String(id))
                    continue;
                QVERIFY(p.stops.first().color.lightness() < 30 && p.stops.last().color.lightness() < 30);
                int lightest = 0;
                for (const GradientStop &s : p.stops)
                    lightest = qMax(lightest, s.color.lightness());
                QVERIFY(lightest > 220);
            }
        }
        // Spun metal ends as it starts, so a conical sweep has no seam.
        for (const GradientPreset &p : gradientPresets())
            if (p.id == QLatin1String("spun-metal"))
                QCOMPARE(p.stops.first().color, p.stops.last().color);

        // Shaded: a highlight lighter than the colour, a shadow darker, the colour between.
        const GradientStops shaded = shadedStops(QColor(200, 40, 40));
        QVERIFY(shaded.first().color.lightness() > QColor(200, 40, 40).lightness());
        QVERIFY(shaded.last().color.lightness() < QColor(200, 40, 40).lightness());
        QCOMPARE(shaded.at(1).color, QColor(200, 40, 40));
    }

    void reflectedGradientMirrorsAboutTheStart()
    {
        TileStore store;
        const QRect canvas(0, 0, 200, 100);
        Gradient g;
        g.from = QPointF(100, 50);
        g.to = QPointF(150, 50);
        g.stops = twoStops(Qt::white, Qt::black);
        g.shape = GradientShape::Reflected;
        fillGradient(store, {}, canvas, g);
        QVERIFY(colorAt(store, 100, 50).red() > 245); // lightest along the middle
        QCOMPARE(colorAt(store, 120, 50), colorAt(store, 79, 50)); // the same either side
        QCOMPARE(colorAt(store, 140, 10), colorAt(store, 59, 90));
        QCOMPARE(colorAt(store, 160, 50), QColor(Qt::black));
        QCOMPARE(colorAt(store, 30, 50), QColor(Qt::black));
        QVERIFY(colorAt(store, 120, 50).red() < colorAt(store, 105, 50).red());
    }

    void conicalGradientSweepsClockwiseFromTheLine()
    {
        Gradient g;
        g.from = QPointF(100, 100);
        g.to = QPointF(150, 100); // pointing right
        g.shape = GradientShape::Conical;
        // Pixel centres: a quarter turn clockwise on screen is straight down.
        QVERIFY(std::abs(gradientPosition(g, 99, 139) - 0.25) < 0.02);
        QVERIFY(std::abs(gradientPosition(g, 60, 99) - 0.5) < 0.02);
        QVERIFY(std::abs(gradientPosition(g, 99, 60) - 0.75) < 0.02);
        QVERIFY(gradientPosition(g, 140, 100) < 0.02);
        QVERIFY(gradientPosition(g, 140, 98) > 0.98); // just before a full turn
        // The distance from the centre makes no difference.
        QVERIFY(std::abs(gradientPosition(g, 110, 110) - gradientPosition(g, 180, 180)) < 0.01);
        // The line can point anywhere: straight down, a quarter turn on is to the left.
        g.to = QPointF(100, 150);
        QVERIFY(std::abs(gradientPosition(g, 60, 99) - 0.25) < 0.02);

        // Drawn with a gradient that ends as it starts: no seam along the line.
        TileStore store;
        g.to = QPointF(150, 100);
        for (const GradientPreset &p : gradientPresets())
            if (p.id == QLatin1String("spun-metal"))
                g.stops = p.stops;
        fillGradient(store, {}, QRect(0, 0, 200, 200), g);
        const QColor above = colorAt(store, 160, 99), below = colorAt(store, 160, 100);
        QVERIFY2(std::abs(above.red() - below.red()) <= 3, qPrintable(above.name() + below.name()));
        QVERIFY(colorAt(store, 100, 30) != colorAt(store, 150, 50)); // and it does vary round the centre
    }

    void aMetalPresetDrawsItsBands()
    {
        TileStore store;
        Gradient g;
        g.from = QPointF(0, 0);
        g.to = QPointF(0, 100);
        for (const GradientPreset &p : gradientPresets())
            if (p.id == QLatin1String("chrome"))
                g.stops = p.stops;
        fillGradient(store, {}, QRect(0, 0, 20, 100), g);
        // Chrome's horizon: nearly black just above the middle, bright just below it.
        QVERIFY(colorAt(store, 10, 49).lightness() < 60);
        QVERIFY(colorAt(store, 10, 53).lightness() > 200);
        QVERIFY(colorAt(store, 10, 1).lightness() > 230);
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
