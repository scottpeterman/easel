#include "color.h"
#include "filters.h"
#include "tilestore.h"

#include <QElapsedTimer>
#include <QTest>

using namespace easeletch;

namespace {

QColor colorAt(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

float alphaAt(const TileStore &s, int x, int y)
{
    return float(s.pixel(x, y).a);
}

double alphaSum(const TileStore &s, const QRect &r)
{
    double sum = 0.0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            sum += double(alphaAt(s, x, y));
    return sum;
}

Filter blur(double radius)
{
    Filter f = Filter::make(FilterType::GaussianBlur);
    f.radius = radius;
    return f;
}

} // namespace

class TestFilters : public QObject
{
    Q_OBJECT

private slots:
    void blurSpreadsAndKeepsTheTotal()
    {
        for (const double radius : {1.0, 6.0}) { // the exact kernel, and the box approximation
            TileStore store;
            const QRect canvas(0, 0, 300, 300);
            store.fillRect(QRect(130, 130, 40, 40), QColor(Qt::red));
            const auto before = applyFilter(store, {}, canvas, blur(radius));
            QVERIFY(!before.isEmpty());
            // Nothing is lost or gained, it just spreads.
            QVERIFY2(std::abs(alphaSum(store, canvas) - 1600.0) < 2.0, qPrintable(QString::number(alphaSum(store, canvas))));
            QVERIFY(alphaAt(store, 150, 150) > 0.99f);                    // the middle is still solid
            // Half way at the edge itself, which runs between pixels 129 and 130.
            QVERIFY(std::abs((alphaAt(store, 129, 150) + alphaAt(store, 130, 150)) / 2.0f - 0.5f) < 0.02f);
            QVERIFY(alphaAt(store, 129 - int(radius), 150) > 0.01f);      // and it reaches out
            QCOMPARE(alphaAt(store, 130 - int(radius * 3.0) - 4, 150), 0.0f);
            // Still red: transparency around it doesn't darken the colour.
            const Pixel edge = store.pixel(128, 150);
            QVERIFY(float(edge.g) == 0.0f && float(edge.b) == 0.0f);
            QVERIFY(std::abs(float(edge.r) - float(edge.a)) < 1e-3f);
            // Symmetric.
            QVERIFY(std::abs(alphaAt(store, 127, 150) - alphaAt(store, 172, 150)) < 1e-3f);
            QVERIFY(std::abs(alphaAt(store, 150, 127) - alphaAt(store, 172, 150)) < 1e-3f);
        }
    }

    void blurMatchesAGaussian()
    {
        // A hard edge blurred at sigma 8: 16% / 84% of the way one sigma either side.
        TileStore store(Qt::black);
        const QRect canvas(0, 0, 400, 100);
        store.fillRect(QRect(200, 0, 200, 100), QColor(Qt::white));
        applyFilter(store, {}, canvas, blur(8.0));
        QVERIFY(std::abs(float(store.pixel(199, 50).r) - 0.5f) < 0.04f);
        QVERIFY(std::abs(float(store.pixel(207, 50).r) - 0.84f) < 0.04f);
        QVERIFY(std::abs(float(store.pixel(191, 50).r) - 0.16f) < 0.04f);
        // A flat picture stays flat, right to the canvas edge.
        QCOMPARE(colorAt(store, 0, 0), QColor(Qt::black));
        QCOMPARE(colorAt(store, 399, 99), QColor(Qt::white));
    }

    void emptyLayersAndFlatAreasAreLeftAlone()
    {
        TileStore empty;
        QVERIFY(applyFilter(empty, {}, QRect(0, 0, 500, 500), blur(10.0)).isEmpty());
        QCOMPARE(empty.tileCount(), 0);

        // Only tiles near the painted part are touched.
        TileStore store;
        store.fillRect(QRect(10, 10, 20, 20), QColor(Qt::red));
        const auto before = applyFilter(store, {}, QRect(0, 0, 2000, 2000), blur(4.0));
        QVERIFY(before.size() <= 4);
        QVERIFY(store.tileCount() <= 4);
    }

    void staysInsideTheSelection()
    {
        TileStore store(Qt::black);
        const QRect canvas(0, 0, 200, 100);
        store.fillRect(QRect(100, 0, 100, 100), QColor(Qt::white));
        applyFilter(store, Selection::rect(QRect(0, 0, 200, 50)), canvas, blur(6.0));
        QVERIFY(float(store.pixel(98, 20).r) > 0.2f);  // blurred above
        QCOMPARE(colorAt(store, 98, 80), QColor(Qt::black)); // sharp below
        QCOMPARE(colorAt(store, 101, 80), QColor(Qt::white));
        // The blur inside reads the pixels just outside it: no seam at the selection's edge.
        QVERIFY(std::abs(float(store.pixel(98, 49).r) - float(store.pixel(98, 20).r)) < 0.01f);
    }

    void sharpenSteepensEdgesAndLeavesFlatAreas()
    {
        // A soft edge: grey 100 to grey 160 through a 3 px ramp.
        TileStore store(QColor(100, 100, 100));
        const QRect canvas(0, 0, 200, 100);
        store.fillRect(QRect(100, 0, 100, 100), QColor(160, 160, 160));
        store.fillRect(QRect(99, 0, 1, 100), QColor(130, 130, 130));
        Filter f = Filter::make(FilterType::Sharpen);
        f.amount = 1.5;
        f.radius = 2.0;
        applyFilter(store, {}, canvas, f);
        QVERIFY(colorAt(store, 97, 50).red() < 100);  // darker on the dark side of the edge
        QVERIFY(colorAt(store, 101, 50).red() > 160); // lighter on the light side
        QCOMPARE(colorAt(store, 40, 50).rgb(), QColor(100, 100, 100).rgb()); // away from any edge: unchanged
        QCOMPARE(colorAt(store, 180, 50).rgb(), QColor(160, 160, 160).rgb());
        QCOMPARE(colorAt(store, 40, 50).alpha(), 255);

        // Nothing at all happens at amount 0.
        TileStore flat(QColor(100, 100, 100));
        flat.fillRect(QRect(50, 0, 50, 50), QColor(Qt::white));
        f.amount = 0.0;
        QVERIFY(applyFilter(flat, {}, canvas, f).isEmpty());
    }

    void noiseIsRepeatableAndKeepsTransparency()
    {
        const QRect canvas(0, 0, 128, 128);
        TileStore a(QColor(128, 128, 128)), b(QColor(128, 128, 128));
        a.fillRect(QRect(0, 0, 64, 128), QColor(0, 0, 0, 0)); // the left half is empty
        b.fillRect(QRect(0, 0, 64, 128), QColor(0, 0, 0, 0));
        Filter f = Filter::make(FilterType::Noise);
        f.amount = 0.3;
        applyFilter(a, {}, canvas, f);
        applyFilter(b, {}, canvas, f);
        int differing = 0, coloured = 0;
        double sum = 0.0;
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                QVERIFY(samePixel(a.pixel(x, y), b.pixel(x, y))); // same seed, same pattern
                if (x < 64) {
                    QCOMPARE(alphaAt(a, x, y), 0.0f);
                    continue;
                }
                QCOMPARE(alphaAt(a, x, y), 1.0f);
                const QColor c = colorAt(a, x, y);
                differing += c != QColor(128, 128, 128);
                coloured += c.red() != c.green() || c.green() != c.blue();
                sum += c.red();
            }
        }
        QVERIFY(differing > 7000);
        QVERIFY(coloured > 6000);                              // coloured specks
        QVERIFY(std::abs(sum / (64 * 128) - 128.0) < 4.0);     // lighter and darker in equal measure

        // Another seed: another pattern. Monochrome: grey stays grey.
        f.seed = 2;
        f.monochrome = true;
        TileStore c(QColor(128, 128, 128));
        applyFilter(c, {}, canvas, f);
        int same = 0;
        for (int i = 64; i < 128; ++i) {
            const QColor m = colorAt(c, i, i);
            QVERIFY(m.red() == m.green() && m.green() == m.blue());
            same += samePixel(c.pixel(i, i), a.pixel(i, i));
        }
        QVERIFY(same < 8);
    }

    void pixelateAveragesBlocks()
    {
        // Black and white 1 px stripes in 8 px blocks: every block is their average.
        TileStore store(Qt::black);
        const QRect canvas(0, 0, 64, 64);
        for (int x = 0; x < 64; x += 2)
            store.fillRect(QRect(x, 0, 1, 64), QColor(Qt::white));
        Filter f = Filter::make(FilterType::Pixelate);
        f.cell = 8;
        applyFilter(store, {}, canvas, f);
        for (int y = 0; y < 64; y += 5)
            for (int x = 0; x < 64; x += 3)
                QVERIFY2(std::abs(float(store.pixel(x, y).r) - 0.5f) < 0.01f, qPrintable(QString::number(x)));

        // A coloured square off the block grid: blocks are flat, and line up with the canvas.
        TileStore s2;
        s2.fillRect(QRect(13, 13, 20, 20), QColor(Qt::red));
        f.cell = 10;
        applyFilter(s2, {}, QRect(0, 0, 100, 100), f);
        QVERIFY(samePixel(s2.pixel(10, 10), s2.pixel(19, 19)));
        QVERIFY(samePixel(s2.pixel(20, 20), s2.pixel(29, 29)));
        QVERIFY(!samePixel(s2.pixel(19, 19), s2.pixel(20, 20)));
        QVERIFY(std::abs(alphaAt(s2, 15, 15) - 0.49f) < 0.01f); // 7 x 7 of 10 x 10 was painted
        QCOMPARE(alphaAt(s2, 25, 25), 1.0f);
        QCOMPARE(alphaAt(s2, 45, 45), 0.0f);
    }

    void aFeatheredSelectionFadesTheEffect()
    {
        TileStore store(Qt::black);
        const QRect canvas(0, 0, 200, 200);
        for (int x = 0; x < 200; x += 2)
            store.fillRect(QRect(x, 0, 1, 200), QColor(Qt::white));
        const Selection soft = Selection::rect(QRect(60, 60, 80, 80)).feathered(8, canvas);
        applyFilter(store, soft, canvas, blur(4.0));
        const auto contrast = [&](int x, int y) {
            return std::abs(float(store.pixel(x, y).r) - float(store.pixel(x + 1, y).r));
        };
        QVERIFY(contrast(100, 100) < 0.05f);                          // fully blurred in the middle
        QVERIFY(contrast(10, 10) > 0.95f);                            // untouched outside
        QVERIFY(contrast(60, 100) > 0.2f && contrast(60, 100) < 0.8f); // part way at the soft edge
    }

    void largeBlurIsQuickEnoughToPreview()
    {
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 2000, 1500);
        store.fillRect(QRect(200, 200, 1600, 1100), QColor(40, 90, 200));
        QElapsedTimer timer;
        timer.start();
        applyFilter(store, {}, canvas, blur(40.0));
        const qint64 ms = timer.elapsed();
        qInfo("Gaussian blur, radius 40, 2000 x 1500: %lld ms", ms);
        QVERIFY2(ms < 3000, "too slow to preview");
    }
};

QTEST_GUILESS_MAIN(TestFilters)
#include "tst_filters.moc"
