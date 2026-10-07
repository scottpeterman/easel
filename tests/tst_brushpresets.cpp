#include "brushpresets.h"

#include <QPainter>
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
                                      QStringLiteral("Paint")}));
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
        if (!dir.isEmpty())
            sheet.save(dir + QStringLiteral("/brush-previews.png"));
    }
};

QTEST_MAIN(TestBrushPresets)
#include "tst_brushpresets.moc"
