#include "color.h"
#include "shade.h"
#include "tilestore.h"

#include <QElapsedTimer>
#include <QTest>

using namespace easeletch;

namespace {

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

int grey(const TileStore &s, int x, int y)
{
    return at(s, x, y).red();
}

const QRect kCanvas(0, 0, 400, 300);

// White paper with two boxes drawn in black 4 px lines, side by side and
// sharing a wall: A is (50..150, 50..150), B is (150..250, 50..150).
TileStore twoBoxes()
{
    TileStore s(Qt::white);
    s.fillRect(QRect(50, 50, 204, 104), QColor(Qt::black));
    s.fillRect(QRect(54, 54, 96, 96), QColor(Qt::white));
    s.fillRect(QRect(154, 54, 96, 96), QColor(Qt::white));
    return s;
}

ShadeSettings plain()
{
    ShadeSettings s;
    s.stops = twoStops(Qt::white, Qt::black);
    s.angle = 0.0;
    s.variation = 0.0;
    return s;
}

} // namespace

class TestShade : public QObject
{
    Q_OBJECT

private slots:
    void findsTheEnclosedAreasAndNotThePage()
    {
        const TileStore art = twoBoxes();
        AreaShader shader;
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        QCOMPARE(shader.areaCount(), 2);
        const int a = shader.areaAt(100, 100), b = shader.areaAt(200, 100);
        QVERIFY(a >= 0 && b >= 0 && a != b);
        QCOMPARE(shader.areaAt(10, 10), -1);   // the page round the drawing
        QCOMPARE(shader.areaAt(300, 200), -1);
        // Each reaches three pixels under the lines round it, and no further.
        QCOMPARE(shader.areaAt(52, 100), a);
        QCOMPARE(shader.areaAt(50, 100), -1);
        QCOMPARE(shader.areaAt(151, 100), a);
        QCOMPARE(shader.areaAt(152, 100), b);
    }

    void eachAreaGetsItsOwnRunOfTheGradient()
    {
        const TileStore art = twoBoxes();
        AreaShader shader;
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        TileStore shading;
        const auto before = shader.shade(shading, plain());
        QVERIFY(!before.isEmpty());
        // White at each box's left side, black at its right: the second box
        // starts over, it doesn't carry on from the first.
        QVERIFY(grey(shading, 56, 100) > 235);
        QVERIFY(grey(shading, 147, 100) < 60);
        QVERIFY(grey(shading, 157, 100) > 235);
        QVERIFY(grey(shading, 247, 100) < 60);
        QVERIFY(qAbs(grey(shading, 100, 60) - grey(shading, 100, 140)) <= 1); // the same all the way down
        // Nothing outside the areas.
        QCOMPARE(at(shading, 20, 20).alpha(), 0);
        QCOMPARE(at(shading, 300, 100).alpha(), 0);
        QCOMPARE(at(shading, 100, 100).alpha(), 255);

        // Top to bottom at 90 degrees.
        ShadeSettings down = plain();
        down.angle = 90.0;
        shader.shade(shading, down);
        QVERIFY(grey(shading, 100, 56) > 235 && grey(shading, 100, 147) < 60);
        QVERIFY(qAbs(grey(shading, 60, 100) - grey(shading, 140, 100)) <= 1);
    }

    void aLooseSelectionPicksWholeAreas()
    {
        const TileStore art = twoBoxes();
        AreaShader shader;
        // Round most of box A, and a corner of box B.
        shader.findAreas(art, Selection::rect(QRect(40, 40, 130, 90)), kCanvas, AreaOptions());
        QCOMPARE(shader.areaCount(), 1);
        QVERIFY(shader.areaAt(100, 100) >= 0);
        QVERIFY(shader.areaAt(100, 145) >= 0);  // all of A, the part outside the selection too
        QCOMPARE(shader.areaAt(200, 100), -1);  // none of B
    }

    void smallAreasAndColouredOnesAreLeftAlone()
    {
        TileStore art = twoBoxes();
        art.fillRect(QRect(90, 90, 14, 14), QColor(Qt::black));   // a rivet with a white centre in box A
        art.fillRect(QRect(94, 94, 6, 6), QColor(Qt::white));
        art.fillRect(QRect(154, 54, 96, 96), QColor(250, 220, 40)); // box B already yellow
        AreaShader shader;
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        QCOMPARE(shader.areaCount(), 1);
        QCOMPARE(shader.areaAt(200, 100), -1);
        QCOMPARE(shader.areaAt(97, 97), -1); // the rivet's centre: too small
        QVERIFY(shader.areaAt(91, 91) >= 0); // under the rivet's ink, as under any line: three pixels in
        QCOMPARE(shader.areaAt(93, 93), -1); // and no further
        QVERIFY(shader.areaAt(70, 70) >= 0);

        AreaOptions all;
        all.whiteOnly = false;
        shader.findAreas(art, {}, kCanvas, all);
        QCOMPARE(shader.areaCount(), 2);
        QVERIFY(shader.areaAt(200, 100) >= 0);
        all.minArea = 20;
        shader.findAreas(art, {}, kCanvas, all);
        QCOMPARE(shader.areaCount(), 3); // now the rivet's centre counts
    }

    void emptyLayersCountAsWhite()
    {
        // Ink alone on a transparent layer: the boxes are still areas.
        TileStore art;
        art.fillRect(QRect(50, 50, 204, 104), QColor(Qt::black));
        art.fillRect(QRect(54, 54, 96, 96), makePixel(0, 0, 0, 0));
        art.fillRect(QRect(154, 54, 96, 96), makePixel(0, 0, 0, 0));
        AreaShader shader;
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        QCOMPARE(shader.areaCount(), 2);
        // A gap in a wall joins a box to the page, and then it isn't enclosed.
        art.fillRect(QRect(50, 90, 4, 10), makePixel(0, 0, 0, 0));
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        QCOMPARE(shader.areaCount(), 1);
        QCOMPARE(shader.areaAt(100, 100), -1);
    }

    void tintBrightnessAndVariation()
    {
        ShadeSettings s;
        s.stops = {{0.0, QColor(255, 255, 255)}, {0.5, QColor(128, 128, 128)}, {1.0, QColor(40, 40, 40)}};
        s.variation = 0.0;
        // Tint: the mid tones take the colour; the brightest keeps some white.
        s.tint = QColor(220, 30, 30);
        GradientStops red = s.stopsFor(0.0);
        QVERIFY(red.at(1).color.red() > 100 && red.at(1).color.green() < 25 && red.at(1).color.blue() < 25);
        QVERIFY(red.at(0).color.green() > 120 && red.at(0).color.red() > 230); // a pale highlight, not flat red
        QVERIFY(red.at(2).color.red() < 45);
        // Brightness: toward white, or toward black.
        s.tint = QColor();
        s.brightness = 0.5;
        QVERIFY(s.stopsFor(0.0).at(1).color.red() > 185);
        s.brightness = -0.5;
        QVERIFY(s.stopsFor(0.0).at(1).color.red() < 70);
        // Variation: an area's shift moves its tone, and by no more than the setting.
        s.brightness = 0.0;
        s.variation = 0.2;
        const int lighter = s.stopsFor(0.5).at(1).color.red(), darker = s.stopsFor(-0.5).at(1).color.red();
        QVERIFY(lighter > 128 && darker < 128 && lighter - darker < 60);
        // Out of range settings are brought back in.
        s.angle = -90.0;
        s.brightness = 4.0;
        QCOMPARE(s.normalized().angle, 270.0);
        QCOMPARE(s.normalized().brightness, 1.0);
    }

    void shadingAgainReplacesWhatWasThere()
    {
        const TileStore art = twoBoxes();
        AreaShader shader;
        shader.findAreas(art, {}, kCanvas, AreaOptions());
        TileStore shading;
        const TileStore empty = shading;
        const auto first = shader.shade(shading, plain());
        ShadeSettings dark = plain();
        dark.brightness = -1.0;
        shader.shade(shading, dark);
        QCOMPARE(at(shading, 60, 100), QColor(Qt::black));
        // What shade() returns puts the layer back as it was.
        TileStore back = shading;
        for (auto it = first.cbegin(); it != first.cend(); ++it)
            back.setTile(it.key(), it.value());
        for (int y = 0; y < 300; y += 7)
            for (int x = 0; x < 400; x += 7)
                QVERIFY(samePixel(back.pixel(x, y), empty.pixel(x, y)));
    }

    void aDrawingsWorthOfPanelsIsQuick()
    {
        // A page of 2700 x 3100 with a few hundred panels ruled on it.
        const QRect canvas(0, 0, 2700, 3100);
        TileStore art(Qt::white);
        for (int x = 200; x <= 2500; x += 115)
            art.fillRect(QRect(x, 200, 5, 2705), QColor(Qt::black));
        for (int y = 200; y <= 2900; y += 135)
            art.fillRect(QRect(200, y, 2305, 5), QColor(Qt::black));
        AreaShader shader;
        QElapsedTimer timer;
        timer.start();
        shader.findAreas(art, {}, canvas, AreaOptions());
        const qint64 find = timer.restart();
        TileStore shading;
        shader.shade(shading, ShadeSettings());
        const qint64 shade = timer.elapsed();
        qInfo("Shade Areas, 2700 x 3100, %d areas: found in %lld ms, shaded in %lld ms", shader.areaCount(), find, shade);
        QCOMPARE(shader.areaCount(), 20 * 20);
        QVERIFY2(find < 6000, "finding the areas is too slow");
        QVERIFY2(shade < 1500, "too slow to preview");
    }
};

QTEST_GUILESS_MAIN(TestShade)
#include "tst_shade.moc"
