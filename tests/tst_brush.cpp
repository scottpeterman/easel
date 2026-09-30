#include "brush.h"

#include <QTest>

using namespace easel;

namespace {

BrushSettings hard(double size)
{
    BrushSettings s;
    s.size = size;
    s.hardness = 1.0;
    s.opacity = 1.0;
    s.flow = 1.0;
    s.spacing = 0.1;
    s.stabilizer = 0.0;
    s.pressureSize = false;
    s.pressureOpacity = false;
    return s;
}

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

bool near(const QColor &a, const QColor &b, int tol = 1)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol && qAbs(a.alpha() - b.alpha()) <= tol;
}

const QRect kBounds(0, 0, 1000, 1000);

} // namespace

class TestBrush : public QObject
{
    Q_OBJECT

private slots:
    void coverageShape()
    {
        QCOMPARE(dabCoverage(0.0, 10.0, 1.0), 1.0f);
        QCOMPARE(dabCoverage(9.0, 10.0, 1.0), 1.0f);
        QCOMPARE(dabCoverage(10.0, 10.0, 1.0), 0.5f); // rim is antialiased
        QCOMPARE(dabCoverage(11.0, 10.0, 1.0), 0.0f);
        // Soft: full at the centre, half-way down at mid-falloff, zero at the edge.
        QCOMPARE(dabCoverage(0.0, 10.0, 0.0), 1.0f);
        QVERIFY(qAbs(dabCoverage(5.0, 10.0, 0.0) - 0.5f) < 1e-6f);
        QVERIFY(dabCoverage(9.9, 10.0, 0.0) < 0.01f);
    }

    void singleClickPaintsOneDab()
    {
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, hard(20), Qt::red, BrushMode::Paint, {{50, 50}, 1.0});
        const auto before = stroke.end();
        QCOMPARE(stroke.dabCount(), 1);
        QVERIFY(near(at(s, 50, 50), Qt::red));
        QVERIFY(near(at(s, 50, 62), Qt::white));
        QCOMPARE(before.size(), 1);             // one tile touched
        QVERIFY(before.value({0, 0}).isNull()); // and it didn't exist before
    }

    void opacityCapsWithinAStroke()
    {
        // Two overlapping dabs at 50% opacity stay at 50%, not 75%.
        TileStore s(QColor(0, 0, 0, 0));
        BrushSettings b = hard(20);
        b.opacity = 0.5;
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::white, BrushMode::Paint, {{50, 50}, 1.0});
        stroke.moveTo({{52, 50}, 1.0});
        stroke.end();
        QVERIFY(qAbs(float(s.pixel(51, 50).a) - 0.5f) < 0.002f);

        // A second stroke builds on the first: 50% over 50% = 75%.
        stroke.begin(&s, kBounds, b, Qt::white, BrushMode::Paint, {{50, 50}, 1.0});
        stroke.end();
        QVERIFY(qAbs(float(s.pixel(51, 50).a) - 0.75f) < 0.002f);
    }

    void flowBuildsUpWithinAStroke()
    {
        TileStore s(QColor(0, 0, 0, 0));
        BrushSettings b = hard(40);
        b.flow = 0.5;
        b.spacing = 0.05; // 2 px apart: many overlapping dabs on the centre pixel
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::white, BrushMode::Paint, {{100, 100}, 1.0});
        stroke.moveTo({{104, 100}, 1.0});
        stroke.end();
        QCOMPARE(stroke.dabCount(), 3);
        // Three dabs of 0.5: 1 - 0.5^3 = 0.875.
        QVERIFY(qAbs(float(s.pixel(102, 100).a) - 0.875f) < 0.002f);
    }

    void eraserRemovesAlpha()
    {
        TileStore s(Qt::white);
        BrushSettings b = hard(10);
        b.opacity = 0.25;
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Erase, {{20, 20}, 1.0});
        stroke.end();
        QVERIFY(qAbs(float(s.pixel(20, 20).a) - 0.75f) < 0.002f);
        QCOMPARE(at(s, 20, 20).red(), 255); // colour unchanged, only less of it
    }

    void pressureScalesSizeAndOpacity()
    {
        BrushSettings b = hard(40);
        b.pressureSize = true;
        b.pressureOpacity = true;
        QCOMPARE(b.diameterAt(0.5), 20.0);
        QCOMPARE(b.diameterAt(0.0), BrushSettings::MinSize);
        QCOMPARE(b.dabStrengthAt(0.25), 0.25);

        TileStore s(QColor(0, 0, 0, 0));
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::white, BrushMode::Paint, {{100, 100}, 0.5});
        stroke.end();
        QVERIFY(qAbs(float(s.pixel(100, 100).a) - 0.5f) < 0.002f);
        QCOMPARE(float(s.pixel(100, 112).a), 0.0f); // radius 10 at half pressure
    }

    void dabsAreEvenlySpacedAcrossSegments()
    {
        BrushSettings b = hard(10);
        b.spacing = 0.25; // 2.5 px
        DabSpacer spacer;
        QList<StrokeSample> dabs;
        spacer.begin({{0, 0}, 1.0}, dabs);
        spacer.moveTo({{1, 0}, 1.0}, b, dabs);  // too short for a dab
        spacer.moveTo({{6, 0}, 1.0}, b, dabs);  // dabs at 2.5 and 5.0
        spacer.moveTo({{10, 0}, 1.0}, b, dabs); // 7.5 and 10.0
        QCOMPARE(dabs.size(), 5);
        for (int i = 0; i < dabs.size(); ++i)
            QVERIFY(qAbs(dabs[i].pos.x() - 2.5 * i) < 1e-9);
    }

    void stabilizerSmoothsAndCatchesUp()
    {
        Stabilizer st;
        st.setStrength(0.0);
        QCOMPARE(st.window(), 1);
        st.setStrength(1.0);
        QCOMPARE(st.window(), 32);

        st.setStrength(0.1); // window 4
        st.reset({{0, 0}, 1.0});
        const StrokeSample a = st.add({{0, 10}, 1.0});
        const StrokeSample b = st.add({{0, -10}, 1.0});
        QVERIFY(qAbs(a.pos.y()) < 10.0); // jitter damped
        QVERIFY(qAbs(b.pos.y()) < 10.0);
        const auto rest = st.flush();
        QVERIFY(!rest.isEmpty());
        QCOMPARE(rest.last().pos, QPointF(0, -10)); // ends where the pen lifted
    }

    void clippedToCanvas()
    {
        // A 120 px dab at the corner of a 100x100 canvas. Unclipped it would
        // reach tiles 2 (x or y 128..191); clipped it stays in tiles 0..1.
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, QRect(0, 0, 100, 100), hard(120), Qt::red, BrushMode::Paint, {{99, 99}, 1.0});
        stroke.end();
        QCOMPARE(s.tileCount(), 4);
        QVERIFY(near(at(s, 99, 99), Qt::red));
        QVERIFY(near(at(s, 100, 100), Qt::white)); // outside the canvas: untouched default
    }
};

QTEST_GUILESS_MAIN(TestBrush)
#include "tst_brush.moc"
