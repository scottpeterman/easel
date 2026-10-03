#include "adjust.h"
#include "color.h"
#include "documentio.h"
#include "layerstack.h"
#include "tilestore.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace easeletch;

namespace {

// One straight sRGB colour through an adjustment, back as sRGB.
QColor adjusted(const Adjustment &a, const QColor &in, float amount = 1.0f)
{
    const Pixel p = pixelFromColor(in);
    float px[4] = {float(p.r), float(p.g), float(p.b), float(p.a)};
    AdjustmentKernel(a).apply(px, 1, amount);
    return pixelToColor(makePixel(px[0], px[1], px[2], px[3]));
}

bool nearly(const QColor &a, const QColor &b, int tolerance = 2)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance && std::abs(a.alpha() - b.alpha()) <= tolerance;
}

QColor colorAt(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

Layer adjustmentLayer(const Adjustment &a, const QString &name = QStringLiteral("Adjust"))
{
    Layer l;
    l.name = name;
    l.adjust = a;
    return l;
}

} // namespace

class TestAdjust : public QObject
{
    Q_OBJECT

private slots:
    void defaultsChangeNothing()
    {
        const QColor samples[] = {QColor(10, 200, 90), QColor(128, 128, 128), QColor(255, 0, 0, 128), Qt::black, Qt::white};
        for (const AdjustmentType t : {AdjustmentType::BrightnessContrast, AdjustmentType::Levels, AdjustmentType::Curves,
                                       AdjustmentType::HueSaturation, AdjustmentType::Exposure})
            for (const QColor &c : samples)
                QVERIFY2(nearly(adjusted(Adjustment::make(t), c), c, 1), qPrintable(adjustmentKey(t) + c.name(QColor::HexArgb)));
    }

    void brightnessAndContrast()
    {
        Adjustment a = Adjustment::make(AdjustmentType::BrightnessContrast);
        a.brightness = 0.2;
        QVERIFY(nearly(adjusted(a, QColor(100, 100, 100)), QColor(151, 151, 151)));
        QVERIFY(nearly(adjusted(a, QColor(240, 240, 240)), QColor(Qt::white), 0)); // clipped, not wrapped
        a.brightness = 0.0;
        a.contrast = 0.5; // twice as steep about middle grey
        QVERIFY(nearly(adjusted(a, QColor(128, 128, 128)), QColor(128, 128, 128)));
        QVERIFY(nearly(adjusted(a, QColor(158, 158, 158)), QColor(188, 188, 188)));
        QVERIFY(nearly(adjusted(a, QColor(98, 98, 98)), QColor(68, 68, 68)));
        a.contrast = -1.0; // none left: all middle grey
        QVERIFY(nearly(adjusted(a, QColor(10, 200, 250)), QColor(128, 128, 128)));
    }

    void levels()
    {
        Adjustment a = Adjustment::make(AdjustmentType::Levels);
        a.inBlack = 50.0 / 255.0;
        a.inWhite = 200.0 / 255.0;
        QVERIFY(nearly(adjusted(a, QColor(50, 50, 50)), QColor(Qt::black), 0));
        QVERIFY(nearly(adjusted(a, QColor(30, 30, 30)), QColor(Qt::black), 0));
        QVERIFY(nearly(adjusted(a, QColor(200, 200, 200)), QColor(Qt::white), 0));
        QVERIFY(nearly(adjusted(a, QColor(125, 125, 125)), QColor(128, 128, 128)));
        // Gamma above 1 lightens the midtones and leaves the ends alone.
        a = Adjustment::make(AdjustmentType::Levels);
        a.gamma = 2.0;
        QVERIFY(nearly(adjusted(a, QColor(64, 64, 64)), QColor(128, 128, 128)));
        QVERIFY(nearly(adjusted(a, QColor(Qt::black)), QColor(Qt::black), 0));
        QVERIFY(nearly(adjusted(a, QColor(Qt::white)), QColor(Qt::white), 0));
        // Output range: nothing darker than 64 or lighter than 191.
        a = Adjustment::make(AdjustmentType::Levels);
        a.outBlack = 64.0 / 255.0;
        a.outWhite = 191.0 / 255.0;
        QVERIFY(nearly(adjusted(a, QColor(Qt::black)), QColor(64, 64, 64)));
        QVERIFY(nearly(adjusted(a, QColor(Qt::white)), QColor(191, 191, 191)));
        // Points pushed past each other are kept apart.
        a.inBlack = 0.9;
        a.inWhite = 0.1;
        const Adjustment n = a.normalized();
        QVERIFY(n.inWhite > n.inBlack);
    }

    void curves()
    {
        // Through its points, flat beyond the ends, never overshooting between.
        const QList<QPointF> pts = {{0.0, 0.0}, {0.25, 0.5}, {0.75, 0.5}, {1.0, 1.0}};
        QCOMPARE(curveValue(pts, 0.25), 0.5);
        QCOMPARE(curveValue(pts, 0.75), 0.5);
        for (double x = 0.25; x <= 0.75; x += 0.05)
            QVERIFY2(std::abs(curveValue(pts, x) - 0.5) < 1e-9, qPrintable(QString::number(x)));
        for (double x = 0.0; x < 1.0; x += 0.01)
            QVERIFY(curveValue(pts, x + 0.01) >= curveValue(pts, x) - 1e-9); // rising points, rising curve
        const QList<QPointF> inner = {{0.2, 0.1}, {0.8, 0.9}};
        QCOMPARE(curveValue(inner, 0.0), 0.1);
        QCOMPARE(curveValue(inner, 1.0), 0.9);
        QCOMPARE(curveValue(inner, 0.5), 0.5);

        Adjustment a = Adjustment::make(AdjustmentType::Curves);
        a.curve = {{0.0, 1.0}, {1.0, 0.0}}; // inverted
        QVERIFY(nearly(adjusted(a, QColor(0, 128, 255)), QColor(255, 127, 0)));
        a.curve = {{0.0, 0.0}, {0.5, 0.75}, {1.0, 1.0}}; // midtones lifted
        QVERIFY(nearly(adjusted(a, QColor(128, 128, 128)), QColor(191, 191, 191)));
        QVERIFY(nearly(adjusted(a, QColor(Qt::black)), QColor(Qt::black), 0));
        QVERIFY(nearly(adjusted(a, QColor(Qt::white)), QColor(Qt::white), 0));

        // Out of order, doubled up or out of range: put right.
        a.curve = {{1.0, 1.0}, {0.5, 2.0}, {0.5, 0.2}, {-1.0, 0.0}};
        const Adjustment n = a.normalized();
        QCOMPARE(n.curve.size(), 3);
        QCOMPARE(n.curve.first(), QPointF(0.0, 0.0));
        QCOMPARE(n.curve.at(1), QPointF(0.5, 1.0));
        a.curve = {{0.3, 0.3}};
        QCOMPARE(a.normalized().curve.size(), 2);
    }

    void hueSaturation()
    {
        Adjustment a = Adjustment::make(AdjustmentType::HueSaturation);
        a.hue = 120.0;
        QVERIFY(nearly(adjusted(a, Qt::red), QColor(0, 255, 0)));
        QVERIFY(nearly(adjusted(a, QColor(0, 255, 0)), QColor(0, 0, 255)));
        QVERIFY(nearly(adjusted(a, QColor(90, 90, 90)), QColor(90, 90, 90))); // greys have no hue to turn
        a.hue = -120.0;
        QVERIFY(nearly(adjusted(a, Qt::red), QColor(0, 0, 255)));

        a = Adjustment::make(AdjustmentType::HueSaturation);
        a.saturation = -1.0;
        const QColor grey = adjusted(a, QColor(200, 40, 40));
        QVERIFY(grey.red() == grey.green() && grey.green() == grey.blue());
        a.saturation = 1.0;
        const QColor vivid = adjusted(a, QColor(160, 120, 120));
        QVERIFY(vivid.red() > 160 && vivid.green() < 120);

        a = Adjustment::make(AdjustmentType::HueSaturation);
        a.lightness = 1.0;
        QVERIFY(nearly(adjusted(a, QColor(200, 40, 40)), QColor(Qt::white), 0));
        a.lightness = -1.0;
        QVERIFY(nearly(adjusted(a, QColor(200, 40, 40)), QColor(Qt::black), 0));
        a.lightness = 0.5;
        QVERIFY(nearly(adjusted(a, QColor(100, 0, 0)), QColor(178, 128, 128)));
    }

    void exposureIsInLinearLight()
    {
        Adjustment a = Adjustment::make(AdjustmentType::Exposure);
        a.exposure = 1.0; // one stop: twice the light
        const Pixel in = pixelFromColor(QColor(100, 100, 100));
        float px[4] = {float(in.r), float(in.g), float(in.b), 1.0f};
        AdjustmentKernel(a).apply(px, 1, 1.0f);
        QVERIFY(std::abs(px[0] - 2.0f * float(in.r)) < 1e-4f);
        QVERIFY(nearly(adjusted(a, QColor(220, 220, 220)), QColor(Qt::white), 0));
        a.exposure = -1.0;
        AdjustmentKernel(a).apply(px, 1, 1.0f);
        QVERIFY(std::abs(px[0] - float(in.r)) < 1e-4f);
    }

    void blackAndWhite()
    {
        Adjustment a = Adjustment::make(AdjustmentType::BlackWhite);
        QVERIFY(nearly(adjusted(a, Qt::red), QColor(77, 77, 77)));    // 0.30
        QVERIFY(nearly(adjusted(a, QColor(0, 255, 0)), QColor(150, 150, 150))); // 0.59
        QVERIFY(nearly(adjusted(a, Qt::white), QColor(Qt::white)));
        // Reds made to count for more: red things come out lighter.
        a.red = 0.9;
        QVERIFY(nearly(adjusted(a, Qt::red), QColor(230, 230, 230)));
    }

    void transparencyAndAmount()
    {
        Adjustment a = Adjustment::make(AdjustmentType::Curves);
        a.curve = {{0.0, 1.0}, {1.0, 0.0}};
        // Half-transparent stays half-transparent; nothing stays nothing.
        const QColor half = adjusted(a, QColor(255, 0, 0, 128));
        QVERIFY(nearly(half, QColor(0, 255, 255, 128)));
        float clear[4] = {0, 0, 0, 0};
        AdjustmentKernel(a).apply(clear, 1, 1.0f);
        QVERIFY(clear[0] == 0.0f && clear[3] == 0.0f);
        // At half amount, halfway (in linear light) to the adjusted colour.
        const Pixel w = pixelFromColor(Qt::white);
        float px[4] = {float(w.r), float(w.g), float(w.b), 1.0f};
        AdjustmentKernel(a).apply(px, 1, 0.5f);
        QVERIFY(std::abs(px[0] - 0.5f) < 0.01f);
        // Per-pixel strength (a mask).
        float two[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        const float strength[2] = {0.0f, 1.0f};
        AdjustmentKernel(a).apply(two, 2, 1.0f, strength);
        QVERIFY(two[0] == 1.0f && two[4] < 0.01f);
    }

    void jsonRoundTrip()
    {
        for (int t = 1; t < AdjustmentTypeCount; ++t) {
            Adjustment a = Adjustment::make(AdjustmentType(t));
            a.brightness = 0.25;
            a.contrast = -0.5;
            a.inBlack = 0.1;
            a.inWhite = 0.8;
            a.gamma = 1.7;
            a.outBlack = 0.05;
            a.outWhite = 0.9;
            a.curve = {{0.0, 0.1}, {0.4, 0.6}, {1.0, 0.95}};
            a.hue = -45.0;
            a.saturation = 0.3;
            a.lightness = -0.2;
            a.exposure = 1.5;
            a.red = 0.5;
            a.green = 0.4;
            a.blue = 0.1;
            const Adjustment back = Adjustment::fromJson(a.toJson());
            QCOMPARE(back.type, a.type);
            // What this kind uses comes back; what it doesn't is left at its default.
            const Pixel p = pixelFromColor(QColor(90, 150, 210));
            float x[4] = {float(p.r), float(p.g), float(p.b), 1.0f}, y[4] = {x[0], x[1], x[2], 1.0f};
            AdjustmentKernel(a).apply(x, 1, 1.0f);
            AdjustmentKernel(back).apply(y, 1, 1.0f);
            QVERIFY(x[0] == y[0] && x[1] == y[1] && x[2] == y[2]);
            QVERIFY(!adjustmentKey(a.type).isEmpty());
            QCOMPARE(adjustmentFromKey(adjustmentKey(a.type)), a.type);
        }
        QCOMPARE(Adjustment::fromJson({}).type, AdjustmentType::None);
    }

    // --- in the layer stack ---

    void adjustsWhatIsBelowOnly()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        stack.active()->store.fillRect(QRect(10, 10, 20, 20), QColor(Qt::red));
        Adjustment invert = Adjustment::make(AdjustmentType::Curves);
        invert.curve = {{0.0, 1.0}, {1.0, 0.0}};
        const int adj = stack.insert(adjustmentLayer(invert), 0, INT_MAX);
        Layer top;
        top.name = QStringLiteral("Top");
        top.store.fillRect(QRect(100, 100, 20, 20), QColor(Qt::red));
        stack.insert(std::move(top), 0, INT_MAX);
        stack.recompositeAll();

        QVERIFY(nearly(colorAt(stack.composite(), 15, 15), QColor(0, 255, 255))); // red below it: inverted
        QCOMPARE(colorAt(stack.composite(), 110, 110), QColor(Qt::red));        // red above it: untouched
        QCOMPARE(colorAt(stack.composite(), 60, 60), QColor(Qt::black));        // white with a tile
        QCOMPARE(colorAt(stack.composite(), 190, 190), QColor(Qt::black));      // ... and without one
        QCOMPARE(colorAt(stack.composite(), 5000, 5000), QColor(Qt::black));    // the default pixel too
        QVERIFY(!stack.layer(adj)->hasPixels());
        QVERIFY(stack.layer(adj)->store.tileCount() == 0);

        // Hidden: as if it weren't there.
        stack.layer(adj)->visible = false;
        stack.recompositeAll();
        QCOMPARE(colorAt(stack.composite(), 15, 15), QColor(Qt::red));
        QCOMPARE(colorAt(stack.composite(), 190, 190), QColor(Qt::white));
        // Half opacity: halfway there.
        stack.layer(adj)->visible = true;
        stack.layer(adj)->opacity = 0.5;
        stack.recompositeAll();
        QVERIFY(std::abs(float(stack.composite().pixel(190, 190).r) - 0.5f) < 0.01f);
    }

    void paintingBelowUpdatesThroughTheAdjustment()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        const int bg = stack.activeId();
        Adjustment bw = Adjustment::make(AdjustmentType::BlackWhite);
        stack.insert(adjustmentLayer(bw), 0, INT_MAX);
        stack.recompositeAll();
        stack.layer(bg)->store.fillRect(QRect(70, 70, 10, 10), QColor(Qt::red));
        stack.updateComposite(); // only the tiles painted
        QVERIFY(nearly(colorAt(stack.composite(), 75, 75), QColor(77, 77, 77)));
        QCOMPARE(colorAt(stack.composite(), 5, 5), QColor(Qt::white));
    }

    void maskLimitsTheAdjustment()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        Adjustment invert = Adjustment::make(AdjustmentType::Curves);
        invert.curve = {{0.0, 1.0}, {1.0, 0.0}};
        const int adj = stack.insert(adjustmentLayer(invert), 0, INT_MAX);
        QVERIFY(stack.addMask(adj));
        stack.layer(adj)->mask = TileStore(Qt::black);
        stack.layer(adj)->mask.fillRect(QRect(0, 0, 50, 50), QColor(Qt::white));
        stack.recompositeAll();
        QCOMPARE(colorAt(stack.composite(), 10, 10), QColor(Qt::black)); // where the mask is white
        QCOMPARE(colorAt(stack.composite(), 60, 10), QColor(Qt::white)); // same tile, mask black
        QCOMPARE(colorAt(stack.composite(), 150, 150), QColor(Qt::white)); // no mask tile: its default, black
        stack.layer(adj)->maskEnabled = false;
        stack.recompositeAll();
        QCOMPARE(colorAt(stack.composite(), 150, 150), QColor(Qt::black));
        QVERIFY(!stack.applyMask(adj)); // there are no pixels to erase
    }

    void insideAGroupItOnlySeesTheGroup()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        Layer g;
        g.group = true;
        g.name = QStringLiteral("Group");
        const int group = stack.insert(std::move(g), 0, INT_MAX);
        Layer dot;
        dot.name = QStringLiteral("Dot");
        dot.store.fillRect(QRect(10, 10, 20, 20), QColor(Qt::red));
        stack.insert(std::move(dot), group, 0);
        Adjustment invert = Adjustment::make(AdjustmentType::Curves);
        invert.curve = {{0.0, 1.0}, {1.0, 0.0}};
        stack.insert(adjustmentLayer(invert), group, INT_MAX);
        stack.recompositeAll();
        QVERIFY(nearly(colorAt(stack.composite(), 15, 15), QColor(0, 255, 255))); // the group's dot
        QCOMPARE(colorAt(stack.composite(), 100, 100), QColor(Qt::white));      // not the background under the group
    }

    void mergeDownBakesItIn()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        const int bg = stack.activeId();
        stack.layer(bg)->store.fillRect(QRect(10, 10, 20, 20), QColor(Qt::red));
        Adjustment invert = Adjustment::make(AdjustmentType::Curves);
        invert.curve = {{0.0, 1.0}, {1.0, 0.0}};
        const int adj = stack.insert(adjustmentLayer(invert), 0, INT_MAX);
        stack.recompositeAll();
        const TileStore before = stack.composite().snapshot();

        QVERIFY(stack.mergeDown(adj));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(stack.activeId(), bg);
        QVERIFY(nearly(colorAt(stack.layer(bg)->store, 15, 15), QColor(0, 255, 255)));
        QCOMPARE(stack.layer(bg)->store.defaultColor(), QColor(Qt::black));
        stack.recompositeAll();
        QCOMPARE(colorAt(stack.composite(), 15, 15), colorAt(before, 15, 15)); // looks the same as before
        QCOMPARE(colorAt(stack.composite(), 150, 150), colorAt(before, 150, 150));

        // Nothing can be merged down into an adjustment layer, and one with
        // nothing below it has nothing to be baked into.
        const int a2 = stack.insert(adjustmentLayer(invert), 0, INT_MAX);
        Layer paint;
        paint.name = QStringLiteral("Paint");
        const int p = stack.insert(std::move(paint), 0, INT_MAX);
        QVERIFY(!stack.mergeDown(p));
        stack.remove(bg);
        QVERIFY(!stack.mergeDown(a2));
    }

    void duplicateAndFlatten()
    {
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(100, 100), QStringLiteral("Background"));
        Adjustment a = Adjustment::make(AdjustmentType::Exposure);
        a.exposure = -1.0;
        const int adj = stack.insert(adjustmentLayer(a), 0, INT_MAX);
        const int copy = stack.duplicate(adj);
        QCOMPARE(stack.layer(copy)->adjust, a);
        stack.recompositeAll();
        // Two stops down in all: a quarter of the light.
        QVERIFY(std::abs(float(stack.composite().pixel(50, 50).r) - 0.25f) < 0.01f);
        stack.flatten(QStringLiteral("Flat"));
        QCOMPARE(stack.count(), 1);
        QVERIFY(stack.active()->hasPixels());
        QVERIFY(std::abs(float(stack.active()->store.pixel(50, 50).r) - 0.25f) < 0.01f);
    }

    void savedAndReopened()
    {
        QTemporaryDir dir;
        LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(200, 200), QStringLiteral("Background"));
        stack.active()->store.fillRect(QRect(10, 10, 20, 20), QColor(Qt::red));

        // No adjustment layers: still the older format, which older builds open.
        const QString plain = dir.filePath(QStringLiteral("plain.easeletch"));
        QVERIFY(saveNativeDocument(plain, stack).isEmpty());
        // (the manifest's version is checked through what a loader accepts)
        QVERIFY(loadNativeDocument(plain).ok());

        Adjustment levels = Adjustment::make(AdjustmentType::Levels);
        levels.inBlack = 0.2;
        levels.gamma = 1.4;
        Layer l = adjustmentLayer(levels, QStringLiteral("Levels 1"));
        l.opacity = 0.75;
        const int adj = stack.insert(std::move(l), 0, INT_MAX);
        stack.addMask(adj);
        stack.layer(adj)->mask.fillRect(QRect(0, 0, 64, 64), QColor(Qt::black));
        Adjustment curves = Adjustment::make(AdjustmentType::Curves);
        curves.curve = {{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}};
        stack.insert(adjustmentLayer(curves, QStringLiteral("Curves 1")), 0, INT_MAX);
        stack.recompositeAll();

        const QString path = dir.filePath(QStringLiteral("adjusted.easeletch"));
        QVERIFY(saveNativeDocument(path, stack).isEmpty());
        const LoadedDocument doc = loadNativeDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.stack->count(), 3);
        const Layer &back = doc.stack->layers().at(1);
        QVERIFY(back.isAdjustment());
        QCOMPARE(back.name, QStringLiteral("Levels 1"));
        QCOMPARE(back.adjust, levels.normalized());
        QCOMPARE(back.opacity, 0.75);
        QVERIFY(back.hasMask);
        QCOMPARE(doc.stack->layers().at(2).adjust.curve, curves.curve);
        // And it looks the same.
        for (const QPoint p : {QPoint(15, 15), QPoint(100, 100), QPoint(30, 50), QPoint(199, 199)})
            QVERIFY(samePixel(doc.stack->composite().pixel(p.x(), p.y()), stack.composite().pixel(p.x(), p.y())));
    }
};

QTEST_GUILESS_MAIN(TestAdjust)
#include "tst_adjust.moc"
