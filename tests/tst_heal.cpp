#include "brush.h"
#include "color.h"
#include "heal.h"
#include "tilestore.h"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>

using namespace easeletch;

namespace {

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol && qAbs(a.blue() - b.blue()) <= tol
           && qAbs(a.alpha() - b.alpha()) <= tol;
}

// Full coverage inside a disc, none outside.
std::vector<float> disc(const QRect &area, const QPointF &centre, double radius, float amount = 1.0f)
{
    std::vector<float> c(size_t(area.width()) * size_t(area.height()), 0.0f);
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (std::hypot(x + 0.5 - centre.x(), y + 0.5 - centre.y()) <= radius)
                c[size_t(y - area.top()) * size_t(area.width()) + size_t(x - area.left())] = amount;
    return c;
}

BrushSettings brush(double size)
{
    BrushSettings s;
    s.size = size;
    s.hardness = 1.0;
    s.pressureSize = false;
    return s;
}

const QRect kCanvas(0, 0, 300, 300);

} // namespace

class TestHeal : public QObject
{
    Q_OBJECT

private slots:
    void removesABlotOnFlatColour()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(145, 145, 10, 10), QColor(Qt::black));
        store.fillRect(QRect(40, 40, 30, 30), QColor(Qt::blue)); // something else, left alone
        const TileStore was = store;
        const QRect area(130, 130, 40, 40);
        QVERIFY(healArea(store, kCanvas, area, disc(area, {150, 150}, 14)));
        for (int y = 130; y < 170; ++y)
            for (int x = 130; x < 170; ++x)
                QVERIFY2(near(at(store, x, y), Qt::white), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
        // Nothing outside the marks is touched.
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x)
                if (std::hypot(x + 0.5 - 150, y + 0.5 - 150) > 14)
                    QVERIFY(samePixel(store.pixel(x, y), was.pixel(x, y)));
        // No marks: nothing to do.
        QVERIFY(!healArea(store, kCanvas, area, std::vector<float>(1600, 0.0f)));
    }

    void carriesTheTextureAcross()
    {
        // Stripes four pixels apart with a blot over them: the stripes carry
        // on through the repair, in step with the ones around it.
        TileStore store(Qt::white);
        for (int x = 0; x < 300; x += 4)
            store.fillRect(QRect(x, 0, 2, 300), QColor(60, 60, 60));
        const TileStore clean = store;
        store.fillRect(QRect(140, 140, 20, 20), QColor(200, 30, 30));
        const QRect area(125, 125, 50, 50);
        QVERIFY(healArea(store, kCanvas, area, disc(area, {150, 150}, 18)));
        for (int y = 125; y < 175; ++y)
            for (int x = 125; x < 175; ++x)
                QVERIFY2(near(at(store, x, y), at(clean, x, y), 3), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
    }

    void evensOutLightAcrossTheRepair()
    {
        // A page that gets steadily lighter to the right: whatever patch is
        // copied in, its tone is brought to match where it lands.
        TileStore store;
        for (int x = 0; x < 300; ++x) {
            const int v = 90 + x / 2;
            store.fillRect(QRect(x, 0, 1, 300), QColor(v, v, v));
        }
        const TileStore clean = store;
        store.fillRect(QRect(140, 140, 20, 20), QColor(Qt::black));
        const QRect area(120, 120, 60, 60);
        QVERIFY(healArea(store, kCanvas, area, disc(area, {150, 150}, 22)));
        for (int y = 128; y < 172; ++y)
            for (int x = 128; x < 172; ++x)
                QVERIFY2(near(at(store, x, y), at(clean, x, y), 6),
                         qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(at(store, x, y).name())));
    }

    void partCoverageGoesPartWay()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(140, 140, 20, 20), QColor(Qt::black));
        const QRect area(130, 130, 40, 40);
        QVERIFY(healArea(store, kCanvas, area, disc(area, {150, 150}, 16, 0.5f)));
        const Pixel p = store.pixel(150, 150);
        QVERIFY2(std::abs(float(p.r) - 0.5f) < 0.02f, qPrintable(QString::number(float(p.r)))); // half way, in linear light
        QCOMPARE(float(p.a), 1.0f);
    }

    void worksOnATransparentLayer()
    {
        // A stray mark on an otherwise empty layer heals to nothing.
        TileStore store;
        store.fillRect(QRect(146, 146, 8, 8), QColor(Qt::red));
        const QRect area(135, 135, 30, 30);
        QVERIFY(healArea(store, kCanvas, area, disc(area, {150, 150}, 12)));
        for (int y = 135; y < 165; ++y)
            for (int x = 135; x < 165; ++x)
                QCOMPARE(float(store.pixel(x, y).a), 0.0f);
    }

    void withNowhereToCopyFromItFillsSmoothly()
    {
        // The marks take up nearly the whole (small) canvas: black on the
        // left edge, white on the right, and a smooth run between them.
        const QRect canvas(0, 0, 40, 40);
        TileStore store(Qt::white);
        store.fillRect(QRect(0, 0, 20, 40), QColor(Qt::black));
        std::vector<float> cov(size_t(40) * 40, 0.0f);
        for (int y = 0; y < 40; ++y)
            for (int x = 3; x < 37; ++x)
                cov[size_t(y) * 40 + size_t(x)] = 1.0f;
        QVERIFY(healArea(store, canvas, canvas, cov));
        float last = -1.0f;
        for (int x = 2; x < 38; ++x) {
            const float v = float(store.pixel(x, 20).r);
            QVERIFY(std::isfinite(v) && v >= -0.001f && v <= 1.001f);
            QVERIFY2(v >= last - 0.01f, qPrintable(QStringLiteral("x=%1").arg(x))); // never doubles back
            last = v;
        }
        QVERIFY(float(store.pixel(8, 20).r) < 0.35f && float(store.pixel(31, 20).r) > 0.65f);
        // Everything marked, with nothing left to go by: left as it is.
        TileStore all(Qt::white);
        QVERIFY(!healArea(all, canvas, canvas, std::vector<float>(1600, 1.0f)));
    }

    void aHealStrokeMarksThenRepairs()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(140, 145, 30, 10), QColor(Qt::black));
        BrushStroke stroke;
        stroke.begin(&store, kCanvas, brush(30), Qt::black, BrushMode::Heal, {QPointF(140, 150), 1.0});
        stroke.moveTo({QPointF(170, 150), 1.0});
        // While the stroke is down, a veil shows what's marked.
        QVERIFY(!near(at(store, 150, 138), Qt::white));
        const auto before = stroke.end();
        QVERIFY(!before.isEmpty());
        for (int y = 130; y < 170; ++y)
            for (int x = 120; x < 190; ++x)
                QVERIFY2(near(at(store, x, y), Qt::white), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
    }

    void aHealStrokeStaysInsideTheSelection()
    {
        // Two blots under one dab; only the one inside the selection goes.
        TileStore store(Qt::white);
        store.fillRect(QRect(125, 145, 10, 10), QColor(Qt::black));
        store.fillRect(QRect(165, 145, 10, 10), QColor(Qt::black));
        BrushStroke stroke;
        stroke.begin(&store, kCanvas, brush(80), Qt::black, BrushMode::Heal, {QPointF(150, 150), 1.0},
                     Selection::rect(QRect(150, 0, 150, 300)));
        stroke.end();
        QVERIFY(near(at(store, 130, 150), Qt::black)); // outside the selection: still there, and no veil left on it
        QVERIFY(near(at(store, 140, 150), Qt::white));
        QVERIFY(near(at(store, 170, 150), Qt::white));
    }

    void cloneCopiesFromTheSource()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(40, 40, 20, 20), QColor(Qt::red));
        const TileStore was = store;
        BrushStroke stroke;
        stroke.setCloneOffset(QPoint(-150, 0)); // copy from 150 px to the left
        stroke.begin(&store, kCanvas, brush(40), Qt::black, BrushMode::Clone, {QPointF(200, 50), 1.0});
        const auto before = stroke.end();
        QVERIFY(!before.isEmpty());
        QVERIFY(near(at(store, 200, 50), Qt::red));   // the square, copied
        QVERIFY(near(at(store, 191, 50), Qt::red));
        QVERIFY(near(at(store, 186, 50), Qt::white)); // and the white beside it
        QVERIFY(near(at(store, 200, 36), Qt::white));
        for (int y = 30; y < 70; ++y)
            for (int x = 30; x < 70; ++x)
                QVERIFY(samePixel(store.pixel(x, y), was.pixel(x, y))); // the source is untouched
    }

    void cloneNeverCopiesItsOwnPaint()
    {
        // One red column, cloned from 10 px to the left along a long drag: it
        // lands once, 10 px over, and isn't picked up again further on.
        TileStore store(Qt::white);
        store.fillRect(QRect(50, 0, 4, 300), QColor(Qt::red));
        BrushStroke stroke;
        stroke.setCloneOffset(QPoint(-10, 0));
        stroke.begin(&store, kCanvas, brush(20), Qt::black, BrushMode::Clone, {QPointF(40, 150), 1.0});
        for (int x = 45; x <= 200; x += 5)
            stroke.moveTo({QPointF(x, 150), 1.0});
        stroke.end();
        QVERIFY(near(at(store, 61, 150), Qt::red));
        for (int x = 66; x < 200; ++x)
            QVERIFY2(near(at(store, x, 150), Qt::white), qPrintable(QString::number(x)));
        // Copying from beyond the edge of the canvas leaves things as they are.
        BrushStroke off;
        off.setCloneOffset(QPoint(-500, 0));
        off.begin(&store, kCanvas, brush(20), Qt::black, BrushMode::Clone, {QPointF(51, 100), 1.0});
        QVERIFY(off.end().isEmpty() || near(at(store, 51, 100), Qt::red));
        QVERIFY(near(at(store, 51, 100), Qt::red));
    }

    void cloneCopiesTransparencyToo()
    {
        TileStore store;
        store.fillRect(QRect(100, 0, 200, 300), QColor(Qt::blue));
        BrushStroke stroke;
        stroke.setCloneOffset(QPoint(-150, 0)); // from the empty part
        stroke.begin(&store, kCanvas, brush(30), Qt::black, BrushMode::Clone, {QPointF(200, 150), 1.0});
        stroke.end();
        QCOMPARE(float(store.pixel(200, 150).a), 0.0f);
        QVERIFY(near(at(store, 200, 120), Qt::blue));
    }

    void healingABigAreaIsQuick()
    {
        TileStore store(QColor(180, 165, 150));
        const QRect canvas(0, 0, 2700, 3100);
        store.fillRect(QRect(1000, 1000, 300, 60), QColor(90, 60, 40));
        BrushStroke stroke;
        QElapsedTimer timer;
        timer.start();
        stroke.begin(&store, canvas, brush(120), Qt::black, BrushMode::Heal, {QPointF(980, 1030), 1.0});
        for (int x = 1000; x <= 1320; x += 20)
            stroke.moveTo({QPointF(x, 1030), 1.0});
        stroke.end();
        const qint64 ms = timer.elapsed();
        qInfo("Heal, a 460 x 120 stroke on 2700 x 3100: %lld ms", ms);
        QVERIFY2(ms < 4000, "too slow");
        QVERIFY(near(at(store, 1150, 1030), QColor(180, 165, 150), 3));
    }
};

QTEST_GUILESS_MAIN(TestHeal)
#include "tst_heal.moc"
