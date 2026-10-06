#include "brush.h"

#include <QTest>

using namespace easeletch;

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

    void pixelModeSetsExactPixels()
    {
        TileStore s(QColor(0, 0, 0, 0));
        BrushSettings b = hard(1);
        b.pixel = true;
        b.hardness = 0.2; // ignored in pixel mode
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::red, BrushMode::Paint, {{10.7, 10.2}, 1.0});
        stroke.moveTo({{15.3, 10.9}, 1.0});
        stroke.end();
        for (int x = 10; x <= 15; ++x)
            QCOMPARE(pixelToColor(s.pixel(x, 10)), QColor(Qt::red)); // solid, unbroken
        QCOMPARE(float(s.pixel(12, 9).a), 0.0f);  // nothing above
        QCOMPARE(float(s.pixel(12, 11).a), 0.0f); // or below
    }

    void selectionClipsPainting()
    {
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, hard(40), Qt::red, BrushMode::Paint, {{100, 100}, 1.0},
                     Selection::rect(QRect(100, 80, 50, 50)));
        stroke.end();
        QVERIFY(near(at(s, 110, 100), Qt::red));
        QVERIFY(near(at(s, 90, 100), Qt::white)); // left of the selection
    }

    void smudgeDragsColour()
    {
        // Red on the left half, white on the right; smudge from red into white.
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 100, 200), Qt::red);
        BrushSettings b = hard(30);
        b.hardness = 0.5;
        b.opacity = 0.8; // strength
        b.spacing = 0.1;
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Smudge, {{80, 100}, 1.0});
        for (int x = 83; x <= 140; x += 3)
            stroke.moveTo({{double(x), 100}, 1.0});
        const auto before = stroke.end();
        QVERIFY(!before.isEmpty());

        const QColor dragged = at(s, 115, 100);
        QVERIFY2(dragged.red() > 230 && dragged.green() < 200, qPrintable(dragged.name())); // reddish now
        QCOMPARE(at(s, 115, 140), QColor(Qt::white)); // away from the stroke: untouched
        QCOMPARE(at(s, 20, 100), QColor(Qt::red));
        QCOMPARE(at(s, 115, 100).alpha(), 255); // smudging never pulls in transparency
    }

    void smudgeWithGrowingPressureKeepsOpacity()
    {
        // A pressure ramp enlarges the brush mid-stroke; the newly covered
        // ring must pick up canvas colour, not transparent black.
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 60, 200), Qt::blue);
        BrushSettings b = hard(40);
        b.pressureSize = true;
        b.opacity = 0.7;
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Smudge, {{50, 100}, 0.2});
        for (int i = 1; i <= 20; ++i)
            stroke.moveTo({{50.0 + i * 2.0, 100}, 0.2 + i * 0.04});
        stroke.end();
        for (int y = 85; y <= 115; ++y)
            for (int x = 40; x <= 110; ++x)
                QVERIFY2(pixelToColor(s.pixel(x, y)).alpha() == 255, qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
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

    void symmetryPaintsTheOtherSideAsThisOneIsPainted()
    {
        // Across an upright line: left and right.
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.setSymmetry(Symmetry::LeftRight, {500, 500});
        stroke.begin(&s, kBounds, hard(20), Qt::red, BrushMode::Paint, {{300, 400}, 1.0});
        stroke.moveTo({{350, 400}, 1.0});
        const auto before = stroke.end();
        QVERIFY(near(at(s, 300, 400), Qt::red));
        QVERIFY(near(at(s, 325, 400), Qt::red));
        QVERIFY(near(at(s, 699, 400), Qt::red));
        QVERIFY(near(at(s, 675, 400), Qt::red));
        QVERIFY(near(at(s, 300, 600), Qt::white));
        QVERIFY(near(at(s, 700, 600), Qt::white));
        // The two sides match pixel for pixel.
        for (int y = 385; y <= 415; ++y)
            for (int x = 285; x <= 365; ++x)
                QCOMPARE(at(s, x, y), at(s, 999 - x, y));
        // Undo covers both: the other side's tiles were recorded too.
        QVERIFY(before.contains(TileStore::tileAt(300, 400)));
        QVERIFY(before.contains(TileStore::tileAt(699, 400)));

        // Across a level line, and across both: four for one.
        TileStore t(Qt::white);
        stroke.setSymmetry(Symmetry::TopBottom, {500, 500});
        stroke.begin(&t, kBounds, hard(20), Qt::red, BrushMode::Paint, {{300, 400}, 1.0});
        stroke.end();
        QVERIFY(near(at(t, 300, 599), Qt::red));
        QVERIFY(near(at(t, 699, 400), Qt::white));
        TileStore q(Qt::white);
        stroke.setSymmetry(Symmetry::Quarters, {500, 500});
        stroke.begin(&q, kBounds, hard(20), Qt::red, BrushMode::Paint, {{300, 400}, 1.0});
        QCOMPARE(stroke.dabCount(), 4);
        stroke.end();
        for (const QPoint p : {QPoint(300, 400), QPoint(699, 400), QPoint(300, 599), QPoint(699, 599)})
            QVERIFY(near(at(q, p.x(), p.y()), Qt::red));
        QVERIFY(near(at(q, 500, 500), Qt::white));

        // The line can be anywhere; the eraser mirrors as the brush does.
        TileStore e(Qt::red);
        stroke.setSymmetry(Symmetry::LeftRight, {200, 0});
        stroke.begin(&e, kBounds, hard(20), Qt::black, BrushMode::Erase, {{150, 100}, 1.0});
        stroke.end();
        QCOMPARE(at(e, 150, 100).alpha(), 0);
        QCOMPARE(at(e, 249, 100).alpha(), 0);
        QVERIFY(near(at(e, 200, 100), Qt::red));

        // In pixel mode one pixel is mirrored by exactly one pixel.
        TileStore p(Qt::white);
        BrushSettings one = hard(1);
        one.pixel = true;
        stroke.setSymmetry(Symmetry::LeftRight, {50, 50});
        stroke.begin(&p, QRect(0, 0, 100, 100), one, Qt::red, BrushMode::Paint, {{10.0, 20.0}, 1.0});
        stroke.end();
        QVERIFY(near(at(p, 10, 20), Qt::red));
        QVERIFY(near(at(p, 89, 20), Qt::red));
        QVERIFY(near(at(p, 88, 20), Qt::white));
        QVERIFY(near(at(p, 90, 20), Qt::white));
        QVERIFY(near(at(p, 9, 20), Qt::white));
        QVERIFY(near(at(p, 11, 20), Qt::white));

        // Smudge works from what's under the brush: it isn't mirrored.
        TileStore m(Qt::white);
        stroke.setSymmetry(Symmetry::Quarters, {500, 500});
        stroke.begin(&m, kBounds, hard(20), Qt::red, BrushMode::Smudge, {{300, 400}, 1.0});
        stroke.moveTo({{340, 400}, 1.0});
        const auto smudged = stroke.end();
        QVERIFY(!smudged.contains(TileStore::tileAt(699, 400)));
        QVERIFY(!smudged.contains(TileStore::tileAt(300, 599)));

        // Off again: one stroke, one mark.
        TileStore off(Qt::white);
        stroke.setSymmetry(Symmetry::Off, {500, 500});
        stroke.begin(&off, kBounds, hard(20), Qt::red, BrushMode::Paint, {{300, 400}, 1.0});
        QCOMPARE(stroke.dabCount(), 1);
        stroke.end();
        QVERIFY(near(at(off, 699, 400), Qt::white));
    }
};

QTEST_GUILESS_MAIN(TestBrush)
#include "tst_brush.moc"
