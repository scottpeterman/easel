#include "brushpresets.h"

#include <QPainter>

#include <cmath>
#include <QSet>
#include <QTest>

using namespace easeletch;

namespace {

const QRect kBounds(0, 0, 400, 200);

// A level stroke across the middle at one pressure.
TileStore strokeWith(const BrushSettings &b, double pressure, Symmetry symmetry = Symmetry::Off)
{
    TileStore s(Qt::white);
    BrushStroke stroke;
    stroke.setSymmetry(symmetry, QPointF(200, 100));
    stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Paint, {{40, 60}, pressure});
    for (int x = 44; x <= 360; x += 4)
        stroke.moveTo({{double(x), 60}, pressure});
    stroke.end();
    return s;
}

// How much ink went down: 0 for a clean sheet, 1 per fully black pixel.
double inkIn(const TileStore &s, const QRect &area)
{
    double total = 0.0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            total += 1.0 - pixelToColor(s.pixel(x, y)).lightnessF();
    return total;
}

BrushSettings stick()
{
    BrushSettings b;
    b.size = 20;
    b.hardness = 1.0;
    b.opacity = 1.0;
    b.flow = 1.0;
    b.spacing = 0.1;
    b.pressureSize = false;
    b.pressureOpacity = false;
    return b;
}

} // namespace

class TestBrushPresets : public QObject
{
    Q_OBJECT

private slots:
    void presetsAreWellFormed()
    {
        QSet<QString> ids;
        QStringList groups;
        for (const BrushPreset &p : brushPresets()) {
            QVERIFY2(!ids.contains(p.id), qPrintable(p.id));
            ids.insert(p.id);
            QVERIFY(!p.name.isEmpty() && !p.group.isEmpty());
            QVERIFY(p.settings.size >= BrushSettings::MinSize && p.settings.size <= BrushSettings::MaxSize);
            QCOMPARE(brushPreset(p.id), &p);
            // Each heading's brushes are listed together.
            if (groups.isEmpty() || groups.last() != p.group) {
                QVERIFY2(!groups.contains(p.group), qPrintable(p.group));
                groups << p.group;
            }
        }
        QCOMPARE(groups, QStringList({QStringLiteral("Pencils"), QStringLiteral("Ink"), QStringLiteral("Charcoal"),
                                      QStringLiteral("Paint"), QStringLiteral("Trees"),
                                      QStringLiteral("Watercolour")}));
        QVERIFY(!brushPreset(QStringLiteral("no-such-brush")));

        // The plain brush is what a brush has always been.
        const BrushPreset *round = brushPreset(defaultBrushPresetId());
        QVERIFY(round);
        const BrushSettings plain;
        QCOMPARE(round->settings.size, plain.size);
        QCOMPARE(round->settings.hardness, plain.hardness);
        QCOMPARE(round->settings.grain, 0.0);
        QCOMPARE(round->settings.jitter, 0.0);
        QCOMPARE(round->settings.minSize, 0.0);
    }

    void aSmoothBrushIsUnchanged()
    {
        // No grain: every pixel under the stroke is solid, as before.
        const TileStore s = strokeWith(stick(), 1.0);
        for (int x = 60; x <= 340; x += 7)
            for (int y = 54; y <= 66; y += 3)
                QCOMPARE(pixelToColor(s.pixel(x, y)), QColor(Qt::black));
    }

    void paperToothIsTheCanvass()
    {
        // The same place has the same height every time, and it varies.
        double low = 1.0, high = 0.0, sum = 0.0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float h = paperTooth(x, y, 2.0);
                QCOMPARE(h, paperTooth(x, y, 2.0));
                QVERIFY(h >= 0.0f && h <= 1.0f);
                low = qMin(low, double(h));
                high = qMax(high, double(h));
                sum += h;
            }
        QVERIFY(low < 0.2 && high > 0.8);
        QVERIFY(qAbs(sum / 4096.0 - 0.5) < 0.05);
        QVERIFY(paperTooth(-5, -9, 2.0) >= 0.0f); // off the canvas's corner is still paper
    }

    void grainLeavesThePaperShowing()
    {
        const QRect body(60, 52, 280, 16); // well inside the stroke
        const double solid = inkIn(strokeWith(stick(), 1.0), body);
        QVERIFY(qAbs(solid - body.width() * body.height()) < 1.0);

        BrushSettings b = stick();
        b.grain = 0.8;
        const double light = inkIn(strokeWith(b, 0.2), body);
        const double heavy = inkIn(strokeWith(b, 1.0), body);
        // A light touch marks the high points only; a heavy one most of it,
        // but never quite all.
        QVERIFY2(light < solid * 0.5, qPrintable(QString::number(light / solid)));
        QVERIFY2(heavy > light * 2.0, qPrintable(QString::number(heavy / solid)));
        QVERIFY2(heavy < solid * 0.99, qPrintable(QString::number(heavy / solid)));
        QVERIFY(light > solid * 0.02); // ... and it does leave a mark

        // More grain, more paper.
        b.grain = 0.4;
        QVERIFY(inkIn(strokeWith(b, 0.2), body) > light);
    }

    void aStrokeGoesDownTheSameWayTwice()
    {
        BrushSettings b = brushPreset(QStringLiteral("charcoal"))->settings; // grain and jitter both
        const TileStore first = strokeWith(b, 0.6), second = strokeWith(b, 0.6);
        for (int y = 30; y < 90; ++y)
            for (int x = 20; x < 380; ++x)
                QCOMPARE(pixelToColor(first.pixel(x, y)), pixelToColor(second.pixel(x, y)));
    }

    void jitterRoughensTheEdgeAndMirrors()
    {
        BrushSettings b = stick();
        b.jitter = 0.3;
        const TileStore plain = strokeWith(stick(), 1.0);
        const TileStore rough = strokeWith(b, 1.0);
        // Ink beyond where the clean stroke's edge is.
        const QRect above(60, 44, 280, 5);
        QCOMPARE(inkIn(plain, above), 0.0);
        QVERIFY(inkIn(rough, above) > 10.0);

        // A mirrored stroke scatters as its original does: the top half's
        // reflection is the bottom half, pixel for pixel.
        const TileStore both = strokeWith(b, 1.0, Symmetry::TopBottom);
        for (int y = 40; y < 80; ++y)
            for (int x = 30; x < 370; x += 3)
                QCOMPARE(pixelToColor(both.pixel(x, y)), pixelToColor(both.pixel(x, 199 - y)));
    }

    void minSizeKeepsSomeWidthAtALightTouch()
    {
        BrushSettings b;
        b.size = 20;
        b.pressureSize = true;
        QCOMPARE(b.diameterAt(1.0), 20.0);
        QCOMPARE(b.diameterAt(0.5), 10.0);
        b.minSize = 0.6;
        QCOMPARE(b.diameterAt(1.0), 20.0);
        QCOMPARE(b.diameterAt(0.0), 12.0);
        QCOMPARE(b.diameterAt(0.5), 16.0);
        b.pressureSize = false;
        QCOMPARE(b.diameterAt(0.1), 20.0);
    }

    // --- Shaped tips, bristles, the knife -------------------------------------

    void aFlatTipTurnsWithTheStroke()
    {
        BrushSettings b = stick();
        b.size = 40;
        b.tip = BrushTip::Flat;
        b.aspect = 0.25;
        // Going across, it paints its full width up and down...
        const TileStore across = strokeWith(b, 1.0);
        QCOMPARE(pixelToColor(across.pixel(200, 60 - 17)), QColor(Qt::black));
        QCOMPARE(pixelToColor(across.pixel(200, 60 + 17)), QColor(Qt::black));
        QCOMPARE(pixelToColor(across.pixel(200, 60 - 23)), QColor(Qt::white));
        // ... with square ends: the corner of the bar is painted, where a
        // round brush of the same size would have curved away from it.
        QCOMPARE(pixelToColor(across.pixel(360, 60 - 16)), QColor(Qt::black));
        BrushSettings round = stick();
        round.size = 40;
        QCOMPARE(pixelToColor(strokeWith(round, 1.0).pixel(360 + 14, 60 - 16)), QColor(Qt::white));
        // It reaches only its thickness past where the stroke ended.
        QCOMPARE(pixelToColor(across.pixel(360 + 8, 60)), QColor(Qt::white));

        // Going down, it's as wide side to side.
        TileStore down(Qt::white);
        BrushStroke stroke;
        stroke.begin(&down, kBounds, b, Qt::black, BrushMode::Paint, {{200, 30}, 1.0});
        for (int y = 34; y <= 170; y += 4)
            stroke.moveTo({{200, double(y)}, 1.0});
        stroke.end();
        QCOMPARE(pixelToColor(down.pixel(200 - 17, 100)), QColor(Qt::black));
        QCOMPARE(pixelToColor(down.pixel(200 + 17, 100)), QColor(Qt::black));
        QCOMPARE(pixelToColor(down.pixel(200 - 23, 100)), QColor(Qt::white));
    }

    void aHeldTipPaintsBroadOneWayAndThinTheOther()
    {
        BrushSettings b = stick();
        b.size = 40;
        b.tip = BrushTip::Flat;
        b.aspect = 0.25;
        b.followStroke = false; // wide side level, whichever way it goes
        const TileStore across = strokeWith(b, 1.0);
        // Along its wide side it draws a line only its thickness deep.
        QCOMPARE(pixelToColor(across.pixel(200, 60)), QColor(Qt::black));
        QCOMPARE(pixelToColor(across.pixel(200, 60 - 8)), QColor(Qt::white));
        QCOMPARE(pixelToColor(across.pixel(200, 60 + 8)), QColor(Qt::white));

        b.angle = 90.0; // stood on end: now the same stroke is broad
        const TileStore upright = strokeWith(b, 1.0);
        QCOMPARE(pixelToColor(upright.pixel(200, 60 - 17)), QColor(Qt::black));
        QCOMPARE(pixelToColor(upright.pixel(200, 60 + 17)), QColor(Qt::black));
    }

    void anOvalTipIsRoundedAtItsEnds()
    {
        BrushSettings b = stick();
        b.size = 40;
        b.tip = BrushTip::Oval;
        b.aspect = 0.5;
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Paint, {{200, 100}, 1.0}); // one dab, wide side level
        stroke.end();
        QCOMPARE(pixelToColor(s.pixel(200, 100)), QColor(Qt::black));
        QCOMPARE(pixelToColor(s.pixel(200 + 17, 100)), QColor(Qt::black)); // out along the wide side
        QCOMPARE(pixelToColor(s.pixel(200, 100 + 8)), QColor(Qt::black));  // half as far across it
        QCOMPARE(pixelToColor(s.pixel(200, 100 + 12)), QColor(Qt::white));
        QCOMPARE(pixelToColor(s.pixel(200 + 16, 100 + 8)), QColor(Qt::white)); // no corner
    }

    void streaksRunAlongTheStrokeAndOpenUpAtALightTouch()
    {
        BrushSettings b = stick();
        b.size = 40;
        b.tip = BrushTip::Flat;
        b.aspect = 0.25;
        b.streaks = 0.85;
        const QRect body(80, 44, 240, 32);
        const double solid = body.width() * body.height();
        const TileStore lightStroke = strokeWith(b, 0.25);
        const double light = inkIn(lightStroke, body) / solid;
        const double heavy = inkIn(strokeWith(b, 1.0), body) / solid;
        QVERIFY2(light < 0.6 && light > 0.05, qPrintable(QString::number(light)));
        QVERIFY2(heavy > light + 0.2, qPrintable(QString::number(heavy)));

        // A streak is a line along the stroke: a row across the stroke's
        // width is much the same from end to end, and rows differ.
        double lowRow = 1.0, highRow = 0.0;
        for (int y = 46; y <= 74; ++y) {
            const double row = inkIn(lightStroke, QRect(80, y, 240, 1)) / 240.0;
            const double left = inkIn(lightStroke, QRect(80, y, 120, 1)) / 120.0;
            const double right = inkIn(lightStroke, QRect(200, y, 120, 1)) / 120.0;
            QVERIFY2(qAbs(left - right) < 0.08, qPrintable(QString::number(y)));
            lowRow = qMin(lowRow, row);
            highRow = qMax(highRow, row);
        }
        QVERIFY2(highRow - lowRow > 0.5, qPrintable(QString::number(highRow - lowRow)));

        // No streaks set, no streaks.
        b.streaks = 0.0;
        QVERIFY(qAbs(inkIn(strokeWith(b, 0.25), body) / solid - 1.0) < 1e-6);
    }

    void aKnifeLaysNoPaintAndPushesWhatIsThere()
    {
        const BrushSettings knife = brushPreset(QStringLiteral("knife"))->settings;
        // On clean paper: nothing.
        const TileStore clean = strokeWith(knife, 1.0);
        QCOMPARE(inkIn(clean, kBounds), 0.0);

        // Across a painted bar: the paint is pulled along with it.
        TileStore s(Qt::white);
        s.fillRect(QRect(100, 30, 30, 60), QColor(Qt::black));
        const double before = inkIn(s, QRect(140, 40, 60, 40));
        QCOMPARE(before, 0.0);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, knife, Qt::red, BrushMode::Paint, {{60, 60}, 1.0});
        for (int x = 64; x <= 300; x += 4)
            stroke.moveTo({{double(x), 60}, 1.0});
        stroke.end();
        QVERIFY(inkIn(s, QRect(140, 40, 60, 40)) > 100.0);
        // None of the brush's own colour went down.
        for (int x = 60; x <= 300; x += 5) {
            const QColor c = pixelToColor(s.pixel(x, 60));
            QVERIFY2(c.red() - c.blue() < 3, qPrintable(QString::number(x)));
        }
        // And only where the knife went.
        QCOMPARE(pixelToColor(s.pixel(115, 32)), QColor(Qt::black));
    }

    void aShapedTipMirrors()
    {
        BrushSettings b = brushPreset(QStringLiteral("flat"))->settings;
        // A slanting stroke, so the tip is at an angle that has to be mirrored too.
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.setSymmetry(Symmetry::Quarters, QPointF(200, 100));
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Paint, {{40, 20}, 1.0});
        for (int i = 1; i <= 40; ++i)
            stroke.moveTo({{40 + i * 3.5, 20 + i * 1.5}, 1.0});
        stroke.end();
        QVERIFY(inkIn(s, QRect(0, 0, 200, 100)) > 500.0);
        for (int y = 0; y < 100; y += 2)
            for (int x = 0; x < 200; x += 3) {
                const QColor c = pixelToColor(s.pixel(x, y));
                QCOMPARE(pixelToColor(s.pixel(399 - x, y)), c);
                QCOMPARE(pixelToColor(s.pixel(x, 199 - y)), c);
                QCOMPARE(pixelToColor(s.pixel(399 - x, 199 - y)), c);
            }
    }

    void aClickWithAShapedTipStillLeavesAMark()
    {
        BrushSettings b = brushPreset(QStringLiteral("dry"))->settings;
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Paint, {{200, 100}, 1.0});
        QCOMPARE(stroke.dabCount(), 0); // waiting to see which way it goes
        const auto touched = stroke.end();
        QVERIFY(!touched.isEmpty());
        QVERIFY(inkIn(s, QRect(170, 80, 60, 40)) > 20.0);
    }

    void everyPresetDrawsAPreview()
    {
        const QSize size(160, 40);
        QImage sheet(size.width() + 120, int(brushPresets().size()) * (size.height() + 2), QImage::Format_RGB32);
        sheet.fill(Qt::white);
        QPainter painter(&sheet);
        int row = 0;
        QList<QImage> seen;
        for (const BrushPreset &p : brushPresets()) {
            const QImage preview = brushPreview(p.settings, size);
            QCOMPARE(preview.size(), size);
            // Something was drawn, in the middle, and the corners are paper.
            double ink = 0.0;
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x)
                    ink += 1.0 - preview.pixelColor(x, y).lightnessF();
            QVERIFY2(ink > 20.0, qPrintable(p.id));
            QCOMPARE(preview.pixelColor(0, 0), QColor(Qt::white));
            QVERIFY2(!seen.contains(preview), qPrintable(p.id + QStringLiteral(" looks like another brush")));
            seen << preview;
            painter.drawImage(0, row * (size.height() + 2), preview);
            painter.drawText(size.width() + 8, row * (size.height() + 2) + 25, p.name);
            ++row;
        }
        painter.end();
        const QString dir = qEnvironmentVariable("EASELETCH_TEST_SHOTS");
        if (!dir.isEmpty()) {
            sheet.save(dir + QStringLiteral("/brush-previews.png"));
            // And at something like their real size.
            const QSize big(420, 110);
            QImage large(big.width(), int(brushPresets().size()) * (big.height() + 2), QImage::Format_RGB32);
            large.fill(Qt::white);
            QPainter p(&large);
            int at = 0;
            for (const BrushPreset &preset : brushPresets())
                p.drawImage(0, at++ * (big.height() + 2), brushPreview(preset.settings, big));
            p.end();
            large.save(dir + QStringLiteral("/brush-previews-large.png"));
        }
    }

    void aColourBufferStrokeOfOneColourMatchesThePlainOne()
    {
        // Jitter too small to change a colour turns on the per-mark colour
        // buffer; what it lays down must be what the plain stroke does.
        BrushSettings soft = stick();
        soft.hardness = 0.3;
        soft.opacity = 0.7;
        soft.flow = 0.4;
        BrushSettings buffered = soft;
        buffered.valueJitter = 1e-9;
        const auto run = [](const BrushSettings &b) {
            TileStore s(Qt::white);
            BrushStroke stroke;
            stroke.begin(&s, kBounds, b, QColor(40, 110, 60), BrushMode::Paint, {{40, 60}, 1.0});
            for (int x = 44; x <= 360; x += 4)
                stroke.moveTo({{double(x), 60 + 20 * std::sin(x / 40.0)}, 1.0});
            stroke.end();
            return s;
        };
        const TileStore a = run(soft), b = run(buffered);
        for (int y = 30; y <= 90; ++y)
            for (int x = 30; x <= 370; ++x) {
                const QColor ca = pixelToColor(a.pixel(x, y)), cb = pixelToColor(b.pixel(x, y));
                QVERIFY2(qAbs(ca.red() - cb.red()) <= 1 && qAbs(ca.green() - cb.green()) <= 1
                             && qAbs(ca.blue() - cb.blue()) <= 1,
                         qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            }
    }

    void foliageScattersLeavesOfManyShades()
    {
        const BrushPreset *f = brushPreset(QStringLiteral("foliage"));
        QVERIFY(f);
        TileStore s(Qt::white);
        BrushStroke stroke;
        const QColor green(60, 120, 50);
        stroke.begin(&s, kBounds, f->settings, green, BrushMode::Paint, {{60, 100}, 1.0});
        for (int x = 64; x <= 340; x += 4)
            stroke.moveTo({{double(x), 100}, 1.0});
        stroke.end();

        QSet<QRgb> shades;
        int inked = 0, bare = 0, offLine = 0;
        for (int y = 72; y <= 128; ++y)
            for (int x = 80; x <= 320; ++x) {
                const QColor c = pixelToColor(s.pixel(x, y));
                if (c == QColor(Qt::white)) {
                    ++bare;
                    continue;
                }
                ++inked;
                if (qAbs(y - 100) > 15)
                    ++offLine;
                if (c.alpha() == 255 && c.lightnessF() < 0.9)
                    shades.insert(c.rgb());
            }
        // Leaves all over the brush, with paper showing between them, in
        // plenty of greens.
        QVERIFY2(inked > 2000, qPrintable(QString::number(inked)));
        QVERIFY2(bare > 1000, qPrintable(QString::number(bare)));
        QVERIFY2(offLine > 300, qPrintable(QString::number(offLine)));
        QVERIFY2(shades.size() > 50, qPrintable(QString::number(shades.size())));
        // Shades of the painting colour, not other colours: green stays on top.
        for (const QRgb c : std::as_const(shades))
            QVERIFY(qGreen(c) >= qRed(c) && qGreen(c) >= qBlue(c));
    }

    void foliagePressedLightlyLaysFewerLeaves()
    {
        const BrushPreset *f = brushPreset(QStringLiteral("foliage"));
        QVERIFY(f);
        BrushSettings b = f->settings;
        b.pressureSize = false; // the same size, only the count changes
        const double light = inkIn(strokeWith(b, 0.1), kBounds);
        const double hard = inkIn(strokeWith(b, 1.0), kBounds);
        QVERIFY2(hard > light * 1.4, qPrintable(QStringLiteral("%1 %2").arg(light).arg(hard)));
    }

    void foliageIsRepeatableAndMirrors()
    {
        const BrushPreset *f = brushPreset(QStringLiteral("foliage"));
        QVERIFY(f);
        const TileStore a = strokeWith(f->settings, 0.8, Symmetry::LeftRight);
        const TileStore b = strokeWith(f->settings, 0.8, Symmetry::LeftRight);
        // The same stroke scatters the same way, and its mirror image with it.
        for (int y = 20; y <= 100; ++y)
            for (int x = 0; x < 400; ++x) {
                QCOMPARE(a.pixel(x, y), b.pixel(x, y));
                const QColor l = pixelToColor(a.pixel(x, y)), r = pixelToColor(a.pixel(399 - x, y));
                QVERIFY2(qAbs(l.lightness() - r.lightness()) <= 2, qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            }
    }

    void barkHasALitEdgeOnTheLightsSideOnly()
    {
        const BrushPreset *bark = brushPreset(QStringLiteral("bark-left"));
        QVERIFY(bark);
        BrushSettings b = bark->settings;
        b.size = 12;
        b.pressureSize = false;
        b.lightAngle = 270.0; // from above
        const QColor ground(128, 128, 128);
        TileStore s(ground);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, QColor(70, 45, 30), BrushMode::Paint, {{40, 100}, 1.0});
        for (int x = 44; x <= 360; x += 4)
            stroke.moveTo({{double(x), 100}, 1.0});
        stroke.end();

        const int groundL = ground.lightness();
        int litAbove = 0, litBelow = 0, darkOnLine = 0;
        for (int x = 80; x <= 320; ++x) {
            for (int y = 86; y <= 99; ++y)
                litAbove += pixelToColor(s.pixel(x, y)).lightness() > groundL + 20;
            for (int y = 101; y <= 114; ++y)
                litBelow += pixelToColor(s.pixel(x, y)).lightness() > groundL + 20;
            darkOnLine += pixelToColor(s.pixel(x, 100)).lightness() < groundL - 40;
        }
        QVERIFY2(darkOnLine > 230, qPrintable(QString::number(darkOnLine)));
        QVERIFY2(litAbove > 240, qPrintable(QString::number(litAbove)));
        QCOMPARE(litBelow, 0);

        // Light from the other side puts the edge there instead.
        b.lightAngle = 90.0;
        TileStore t(ground);
        stroke.begin(&t, kBounds, b, QColor(70, 45, 30), BrushMode::Paint, {{40, 100}, 1.0});
        for (int x = 44; x <= 360; x += 4)
            stroke.moveTo({{double(x), 100}, 1.0});
        stroke.end();
        int below = 0, above = 0;
        for (int x = 80; x <= 320; ++x) {
            for (int y = 101; y <= 114; ++y)
                below += pixelToColor(t.pixel(x, y)).lightness() > groundL + 20;
            for (int y = 86; y <= 99; ++y)
                above += pixelToColor(t.pixel(x, y)).lightness() > groundL + 20;
        }
        QVERIFY(below > 240);
        QCOMPARE(above, 0);
    }

    void aFanTapLeavesAnArcOfSeparateBristles()
    {
        const BrushPreset *fan = brushPreset(QStringLiteral("fan"));
        QVERIFY(fan);
        BrushSettings b = fan->settings;
        b.size = 60;
        b.pressureSize = false;
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, Qt::black, BrushMode::Paint, {{200, 80}, 1.0});
        stroke.end();

        // Across the imprint, one bristle after another with paper between.
        int bestRuns = 0;
        for (int y = 60; y <= 130; ++y) {
            int runs = 0;
            bool inInk = false;
            for (int x = 120; x <= 280; ++x) {
                const bool ink = pixelToColor(s.pixel(x, y)).lightness() < 128;
                if (ink && !inInk)
                    ++runs;
                inInk = ink;
            }
            bestRuns = qMax(bestRuns, runs);
        }
        QVERIFY2(bestRuns >= 8, qPrintable(QString::number(bestRuns)));
        // Held level, the bristles touch down below the handle: it droops.
        const double below = inkIn(s, QRect(120, 81, 160, 60)), above = inkIn(s, QRect(120, 20, 160, 60));
        QVERIFY2(below > above * 3, qPrintable(QStringLiteral("%1 %2").arg(above).arg(below)));
        // And the splay reaches past an ordinary dab of that size.
        QVERIFY(inkIn(s, QRect(120, 0, 40, 200)) + inkIn(s, QRect(240, 0, 40, 200)) > 0.0);
    }

    void aLoadedKnifeSkipsLightlyAndCoversWhenPressed()
    {
        const BrushPreset *knife = brushPreset(QStringLiteral("knife-loaded"));
        QVERIFY(knife);
        QVERIFY(knife->settings.smear == 0.0); // it lays paint
        const auto pull = [&](double pressure) {
            TileStore s(QColor(60, 50, 70));
            BrushStroke stroke;
            stroke.begin(&s, kBounds, knife->settings, Qt::white, BrushMode::Paint, {{40, 100}, pressure});
            for (int x = 44; x <= 360; x += 3)
                stroke.moveTo({{double(x), 100}, pressure});
            stroke.end();
            return s;
        };
        const QRect band(80, 84, 240, 32);
        const auto covered = [&](const TileStore &s) {
            int n = 0;
            for (int y = band.top(); y <= band.bottom(); ++y)
                for (int x = band.left(); x <= band.right(); ++x)
                    n += pixelToColor(s.pixel(x, y)).lightness() > 160;
            return double(n) / (band.width() * band.height());
        };
        const TileStore light = pull(0.2), hard = pull(1.0);
        const double l = covered(light), h = covered(hard);
        QVERIFY2(l > 0.1 && l < 0.65, qPrintable(QString::number(l)));
        QVERIFY2(h > 0.85, qPrintable(QString::number(h)));

        // Where it skips, it skips in streaks the way it was pulled: paint
        // changes to bare far less often along the stroke than across it.
        int along = 0, across = 0;
        for (int y = band.top(); y < band.bottom(); ++y)
            for (int x = band.left(); x < band.right(); ++x) {
                const bool here = pixelToColor(light.pixel(x, y)).lightness() > 160;
                along += here != (pixelToColor(light.pixel(x + 1, y)).lightness() > 160);
                across += here != (pixelToColor(light.pixel(x, y + 1)).lightness() > 160);
            }
        QVERIFY2(across > along * 2, qPrintable(QStringLiteral("%1 %2").arg(along).arg(across)));
    }

    void aLoadedKnifeSkipsTheSameWayWhereverItIsPulled()
    {
        // The streaks belong to the stroke, not to where it is on the canvas:
        // the same pull far from the corner leaves the same pattern.
        const BrushPreset *knife = brushPreset(QStringLiteral("knife-loaded"));
        QVERIFY(knife);
        const QRect big(0, 0, 3000, 2000);
        const auto pull = [&](const QPointF &at) {
            TileStore s(QColor(60, 50, 70));
            BrushStroke stroke;
            stroke.begin(&s, big, knife->settings, Qt::white, BrushMode::Paint, {at, 0.25});
            for (int i = 1; i <= 60; ++i)
                stroke.moveTo({at + QPointF(i * 2.0, i * 1.2), 0.25});
            stroke.end();
            return s;
        };
        const QPointF near(60, 60), far(2600, 1700);
        const TileStore a = pull(near), b = pull(far);
        int same = 0, total = 0;
        for (int y = 30; y < 170; ++y)
            for (int x = 30; x < 200; ++x) {
                const bool pa = pixelToColor(a.pixel(x, y)).lightness() > 160;
                const QPoint q = QPoint(x, y) + (far - near).toPoint();
                const bool pb = pixelToColor(b.pixel(q.x(), q.y())).lightness() > 160;
                if (pa || pb) {
                    ++total;
                    same += pa == pb;
                }
            }
        QVERIFY(total > 500);
        QVERIFY2(same > total * 0.97, qPrintable(QStringLiteral("%1 of %2").arg(same).arg(total)));
    }

    void watercolourStainsAndNeverLightens()
    {
        const BrushPreset *wc = brushPreset(QStringLiteral("wc-round"));
        QVERIFY(wc);
        BrushSettings b = wc->settings;
        b.granulation = 0.0;
        b.pressureSize = false;
        b.size = 30;
        // Pale yellow over a dark ground: a stain only darkens.
        const QColor ground(40, 50, 80);
        TileStore s(ground);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, QColor(250, 235, 140), BrushMode::Paint, {{40, 100}, 1.0});
        for (int x = 44; x <= 360; x += 3)
            stroke.moveTo({{double(x), 100}, 1.0});
        stroke.end();
        int touched = 0;
        for (int y = 70; y <= 130; ++y)
            for (int x = 30; x <= 370; ++x) {
                const QColor c = pixelToColor(s.pixel(x, y));
                QVERIFY(c.red() <= ground.red() + 1 && c.green() <= ground.green() + 1 && c.blue() <= ground.blue() + 1);
                touched += c != ground;
            }
        QVERIFY(touched > 1000);
    }

    void watercolourGlazesDeepenAndKeepTheirHue()
    {
        const BrushPreset *wc = brushPreset(QStringLiteral("wc-round"));
        QVERIFY(wc);
        BrushSettings b = wc->settings;
        b.granulation = 0.0;
        b.pressureSize = false;
        b.size = 40;
        const QColor blue(90, 130, 200);
        const auto pass = [&](TileStore &s, double y) {
            BrushStroke stroke;
            stroke.begin(&s, kBounds, b, blue, BrushMode::Paint, {{40, y}, 1.0});
            for (int x = 44; x <= 360; x += 3)
                stroke.moveTo({{double(x), y}, 1.0});
            stroke.end();
        };
        TileStore s(Qt::white);
        pass(s, 100);
        const QColor once = pixelToColor(s.pixel(200, 100));
        pass(s, 100);
        const QColor twice = pixelToColor(s.pixel(200, 100));
        QVERIFY2(twice.lightness() < once.lightness() - 10, qPrintable(QStringLiteral("%1 %2").arg(once.lightness()).arg(twice.lightness())));
        // Still blue: blue the strongest channel, red the weakest.
        QVERIFY(once.blue() > once.green() && once.green() > once.red());
        QVERIFY(twice.blue() > twice.green() && twice.green() > twice.red());

        // On an empty layer it lays down, and glazes there too.
        TileStore layer;
        pass(layer, 100);
        const Pixel p1 = layer.pixel(200, 100);
        QVERIFY(float(p1.a) > 0.4f);
        pass(layer, 100);
        const Pixel p2 = layer.pixel(200, 100);
        QVERIFY(float(p2.r) < float(p1.r) && float(p2.g) < float(p1.g));
    }

    void watercolourDriesDarkerAtTheEdge()
    {
        const BrushPreset *wc = brushPreset(QStringLiteral("wc-round"));
        QVERIFY(wc);
        BrushSettings b = wc->settings;
        b.granulation = 0.0;
        b.pressureSize = false;
        b.hardness = 1.0;
        b.size = 60;
        TileStore s(Qt::white);
        BrushStroke stroke;
        stroke.begin(&s, kBounds, b, QColor(170, 80, 70), BrushMode::Paint, {{60, 100}, 1.0});
        for (int x = 63; x <= 340; x += 3)
            stroke.moveTo({{double(x), 100}, 1.0});
        // Wet: even right across.
        const double wetEdge = pixelToColor(s.pixel(200, 75)).lightnessF(), wetMiddle = pixelToColor(s.pixel(200, 100)).lightnessF();
        QVERIFY(std::abs(wetEdge - wetMiddle) < 0.02);
        stroke.end();
        // Dry: a darker band just inside the edge, all along it.
        double edge = 0, middle = 0;
        for (int x = 100; x <= 300; ++x) {
            edge += pixelToColor(s.pixel(x, 74)).lightnessF() + pixelToColor(s.pixel(x, 126)).lightnessF();
            middle += 2 * pixelToColor(s.pixel(x, 100)).lightnessF();
        }
        QVERIFY2(edge < middle - 0.03 * 402, qPrintable(QStringLiteral("%1 %2").arg(edge).arg(middle)));
    }

    void watercolourGranulatesInThePaper()
    {
        const BrushPreset *wc = brushPreset(QStringLiteral("wc-wash"));
        QVERIFY(wc);
        const auto spread = [&](double gran) {
            BrushSettings b = wc->settings;
            b.granulation = gran;
            b.size = 120;
            TileStore s(Qt::white);
            BrushStroke stroke;
            stroke.begin(&s, kBounds, b, QColor(60, 80, 140), BrushMode::Paint, {{40, 100}, 1.0});
            for (int x = 44; x <= 360; x += 3)
                stroke.moveTo({{double(x), 100}, 1.0});
            stroke.end();
            double sum = 0, sq = 0;
            int n = 0;
            for (int y = 85; y <= 115; ++y)
                for (int x = 120; x <= 280; ++x) {
                    const double l = pixelToColor(s.pixel(x, y)).lightnessF();
                    sum += l, sq += l * l, ++n;
                }
            const double mean = sum / n;
            return std::sqrt(std::max(0.0, sq / n - mean * mean));
        };
        const double smooth = spread(0.0), grainy = spread(0.8);
        QVERIFY2(smooth < 0.01, qPrintable(QString::number(smooth)));
        QVERIFY2(grainy > smooth + 0.015, qPrintable(QStringLiteral("%1 %2").arg(smooth).arg(grainy)));
    }
};

QTEST_MAIN(TestBrushPresets)
#include "tst_brushpresets.moc"
