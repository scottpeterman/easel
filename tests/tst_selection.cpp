#include "brush.h"
#include "history.h"
#include "regionops.h"

#include <QTest>

#include <cstring>

using namespace easeletch;

namespace {

QColor at(const TileStore &s, int x, int y)
{
    const QColor c = pixelToColor(s.pixel(x, y));
    return QColor(c.red(), c.green(), c.blue(), c.alpha());
}

bool same(const TileStore &a, const TileStore &b)
{
    if (a.tileCount() != b.tileCount())
        return false;
    for (const TileCoord c : a.tileCoords()) {
        if (!b.hasTile(c))
            return false;
        if (std::memcmp(a.tile(c).constBits(), b.tile(c).constBits(), size_t(TileStore::BytesPerTile)) != 0)
            return false;
    }
    return true;
}

const QRect kCanvas(0, 0, 400, 300);

} // namespace

class TestSelection : public QObject
{
    Q_OBJECT

private slots:
    void rectAndEllipseMembership()
    {
        const Selection r = Selection::rect(QRect(10, 10, 5, 5));
        QVERIFY(r.contains(10, 10) && r.contains(14, 14));
        QVERIFY(!r.contains(15, 14) && !r.contains(9, 10));

        const Selection e = Selection::ellipse(QRect(0, 0, 10, 10));
        QVERIFY(e.contains(5, 5));
        QVERIFY(e.contains(0, 4) && e.contains(9, 5)); // edge-centre pixels are in
        QVERIFY(!e.contains(0, 0) && !e.contains(9, 9)); // corners are out
        QCOMPARE(e.translated(QPoint(3, 4)).bounds(), QRect(3, 4, 10, 10));
        QVERIFY(Selection().isEmpty());
        QCOMPARE(r.outline().size(), 5);
    }

    void extractAndClear()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 100, 100), Qt::red);
        const Selection sel = Selection::ellipse(QRect(40, 40, 40, 40));

        const QImage img = extractSelection(s, sel);
        QCOMPARE(img.size(), QSize(40, 40));
        const auto px = [&](int x, int y) {
            return pixelToColor(reinterpret_cast<const Pixel *>(img.constScanLine(y))[x]);
        };
        QCOMPARE(px(20, 20).alpha(), 255);
        QCOMPARE(px(0, 0).alpha(), 0); // outside the ellipse

        const auto before = clearSelection(s, sel, kCanvas);
        QVERIFY(!before.isEmpty());
        QCOMPARE(at(s, 60, 60).alpha(), 0);
        QCOMPARE(at(s, 41, 41), QColor(Qt::red)); // corner of the bounds, outside the ellipse
    }

    void clipboardConversionRoundTrips()
    {
        QImage src(3, 1, QImage::Format_ARGB32);
        src.setPixelColor(0, 0, QColor(255, 0, 0));
        src.setPixelColor(1, 0, QColor(10, 200, 30, 128));
        src.setPixelColor(2, 0, QColor(0, 0, 0, 0));
        const QImage back = toClipboardImage(fromClipboardImage(src));
        QCOMPARE(back.pixelColor(0, 0), QColor(255, 0, 0));
        QVERIFY(qAbs(back.pixelColor(1, 0).green() - 200) <= 2);
        QCOMPARE(back.pixelColor(1, 0).alpha(), 128);
        QCOMPARE(back.pixelColor(2, 0).alpha(), 0);
    }

    void pasteMoveAndCommit()
    {
        TileStore s(Qt::white);
        const TileStore original = s.snapshot();
        QImage content(10, 10, QImage::Format_ARGB32);
        content.fill(Qt::blue);

        FloatingContent f;
        f.paste(&s, fromClipboardImage(content), QPoint(20, 20), {}, kCanvas);
        QVERIFY(f.isActive());
        QCOMPARE(at(s, 25, 25), QColor(Qt::blue));

        f.moveTo(QPoint(100, 100));
        QCOMPARE(at(s, 25, 25), QColor(Qt::white)); // old spot restored
        QCOMPARE(at(s, 105, 105), QColor(Qt::blue));
        f.moveBy(QPoint(1, 0));
        QCOMPARE(f.selection().bounds(), QRect(101, 100, 10, 10));

        const auto before = f.commit();
        QVERIFY(!f.isActive());
        QCOMPARE(at(s, 110, 105), QColor(Qt::blue));
        // Undo data: restoring these tiles gives back the untouched canvas.
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            s.setTile(it.key(), it.value());
        QVERIFY(same(s, original));
    }

    void liftLeavesAHoleAndCancelRestores()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(50, 50, 20, 20), Qt::red);
        const TileStore original = s.snapshot();

        FloatingContent f;
        f.lift(&s, Selection::rect(QRect(50, 50, 20, 20)), kCanvas);
        QVERIFY(f.wasLifted());
        QCOMPARE(at(s, 55, 55), QColor(Qt::red)); // floating in place: looks unchanged

        f.moveBy(QPoint(100, 0));
        QCOMPARE(at(s, 55, 55).alpha(), 0); // the hole it was cut from
        QCOMPARE(at(s, 155, 55), QColor(Qt::red));

        f.cancel();
        QVERIFY(!f.isActive());
        QVERIFY(same(s, original));
    }

    void floatingContentClipsAtCanvasEdgeButKeepsPixels()
    {
        TileStore s(Qt::white);
        QImage content(10, 10, QImage::Format_ARGB32);
        content.fill(Qt::green);
        FloatingContent f;
        f.paste(&s, fromClipboardImage(content), QPoint(395, 10), {}, kCanvas);
        QCOMPARE(at(s, 399, 15), QColor(Qt::green));
        QCOMPARE(at(s, 402, 15), QColor(Qt::white)); // outside the canvas: not written
        f.moveTo(QPoint(300, 10));                   // brought back in: all there
        QCOMPARE(at(s, 309, 15), QColor(Qt::green));
        f.commit();
    }

    void maskSelectionTrimsAndCombines()
    {
        QImage m(10, 10, QImage::Format_Grayscale8);
        m.fill(0);
        for (int y = 2; y < 6; ++y)
            for (int x = 3; x < 5; ++x)
                m.scanLine(y)[x] = 255;
        const Selection s = Selection::mask(QRect(100, 100, 10, 10), m);
        QCOMPARE(s.shape(), Selection::Shape::Mask);
        QCOMPARE(s.bounds(), QRect(103, 102, 2, 4));
        QVERIFY(s.contains(104, 105));
        QVERIFY(!s.contains(105, 105));
        QCOMPARE(s.outlines().size(), 4); // top, bottom, left, right runs
        QCOMPARE(s.translated(QPoint(-100, -100)).bounds(), QRect(3, 2, 2, 4));

        const Selection r = Selection::rect(QRect(104, 102, 4, 1));
        const Selection u = s.united(r);
        QCOMPARE(u.bounds(), QRect(103, 102, 5, 4));
        QVERIFY(u.contains(107, 102));
        QVERIFY(!u.contains(107, 103));
        const Selection d = s.subtracted(r);
        QVERIFY(!d.contains(104, 102));
        QVERIFY(d.contains(103, 102));

        const Selection inv = Selection::rect(QRect(2, 2, 4, 4)).inverted(QRect(0, 0, 8, 8));
        QVERIFY(inv.contains(0, 0));
        QVERIFY(!inv.contains(3, 3));
        QCOMPARE(inv.bounds(), QRect(0, 0, 8, 8));
        QVERIFY(Selection().inverted(QRect(0, 0, 8, 8)) == Selection::rect(QRect(0, 0, 8, 8)));
        QImage empty(3, 3, QImage::Format_Grayscale8);
        empty.fill(0);
        QVERIFY(Selection::mask(QRect(0, 0, 3, 3), empty).isEmpty());
    }

    void growAndShrink()
    {
        const Selection r = Selection::rect(QRect(10, 10, 10, 10));
        const Selection g = r.grown(2, QRect(0, 0, 100, 100));
        QCOMPARE(g.bounds(), QRect(8, 8, 14, 14));
        QVERIFY(g.contains(8, 15));
        QVERIFY(!g.contains(8, 8)); // corners are rounded off
        QCOMPARE(r.grown(2, QRect(0, 0, 21, 21)).bounds(), QRect(8, 8, 13, 13)); // clipped
        const Selection sh = r.grown(-3, QRect(0, 0, 100, 100));
        QCOMPARE(sh.bounds(), QRect(13, 13, 4, 4));
        QVERIFY(r.grown(-5, QRect(0, 0, 100, 100)).isEmpty());
    }

    void magicWandContiguousAndGlobal()
    {
        TileStore s(Qt::black);
        s.fillRect(QRect(10, 10, 20, 20), QColor(200, 40, 40));
        s.fillRect(QRect(60, 10, 5, 5), QColor(205, 45, 38)); // close colour, not connected
        s.fillRect(QRect(15, 15, 2, 2), QColor(20, 20, 20));  // dark specks inside

        const Selection c = magicWand(s, kCanvas, QPoint(12, 12), 0.05, true);
        QCOMPARE(c.bounds(), QRect(10, 10, 20, 20));
        QVERIFY(!c.contains(15, 15)); // the speck is a hole
        QVERIFY(!c.contains(60, 10));

        const Selection g = magicWand(s, kCanvas, QPoint(12, 12), 0.05, false);
        QVERIFY(g.contains(62, 12));
        const Selection exact = magicWand(s, kCanvas, QPoint(12, 12), 0.0, false);
        QVERIFY(!exact.contains(62, 12));

        // Background with dark noise: tolerance takes the specks too.
        const Selection bg = magicWand(s, kCanvas, QPoint(0, 0), 0.1, true);
        QVERIFY(bg.contains(0, 0));
        QVERIFY(!bg.contains(12, 12));
        QVERIFY(!bg.contains(15, 15)); // inside the red: not connected to the outside
    }

    void colorToAlphaUnmixesAndThresholds()
    {
        TileStore s(Qt::black);
        s.fillRect(QRect(0, 0, 1, 1), QColor(128, 0, 0));  // dim red on black: half-transparent red
        s.fillRect(QRect(1, 0, 1, 1), QColor(255, 255, 255));
        s.fillRect(QRect(2, 0, 1, 1), QColor(6, 6, 6));   // noise
        const auto before = colorToAlpha(s, {}, QRect(0, 0, 8, 8), Qt::black, 0.04);
        QVERIFY(!before.isEmpty());

        QCOMPARE(at(s, 5, 5).alpha(), 0);              // the background
        const QColor red = at(s, 0, 0);
        QCOMPARE(red.red(), 255);
        QCOMPARE(red.green(), 0);
        QVERIFY(qAbs(red.alpha() - 124) <= 3);        // (128/255 - 0.04) / 0.96
        QCOMPARE(at(s, 1, 0), QColor(255, 255, 255)); // far from black: untouched
        QCOMPARE(at(s, 2, 0).alpha(), 0);              // under the threshold

        // Inside a selection only.
        TileStore t(Qt::black);
        colorToAlpha(t, Selection::rect(QRect(0, 0, 4, 4)), QRect(0, 0, 8, 8), Qt::black, 0.0);
        QCOMPARE(at(t, 1, 1).alpha(), 0);
        QCOMPARE(at(t, 5, 5), QColor(Qt::black));
    }

    void opaqueBoundsFindsContent()
    {
        TileStore s;
        QVERIFY(opaqueBounds(s, kCanvas).isEmpty());
        s.fillRect(QRect(70, 130, 3, 9), QColor(Qt::green));
        s.fillRect(QRect(200, 10, 1, 1), QColor(0, 0, 0, 1));
        QCOMPARE(opaqueBounds(s, kCanvas), QRect(QPoint(70, 10), QPoint(200, 138)));
        TileStore white(Qt::white);
        QCOMPARE(opaqueBounds(white, kCanvas), kCanvas);
    }

    void cropShiftsPixelsAndDropsTheRest()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(100, 70, 10, 10), QColor(Qt::red));  // inside the crop, off the tile grid
        s.fillRect(QRect(300, 250, 20, 20), QColor(Qt::blue)); // outside it
        const TileStore before = s.snapshot();

        History h;
        h.reset(QStringLiteral("base"));
        QSize size(400, 300);
        const QRect crop(90, 65, 50, 40);
        h.push(QStringLiteral("Crop"), cropStore(s, crop), size);
        size = crop.size();

        QCOMPARE(at(s, 10, 5), QColor(Qt::red));   // (100, 70) moved to (10, 5)
        QCOMPARE(at(s, 9, 5), QColor(Qt::white));
        QCOMPARE(at(s, 19, 14), QColor(Qt::red));
        QCOMPARE(at(s, 20, 15), QColor(Qt::white));
        for (const TileCoord c : s.tileCoords())
            QVERIFY(TileStore::tileRect(c).intersects(QRect(QPoint(0, 0), size)));
        // Beyond the new edge, inside an edge tile: default, not old content.
        QCOMPARE(at(s, 55, 5), QColor(Qt::white));

        h.undo(s, &size);
        QCOMPARE(size, QSize(400, 300));
        QVERIFY(same(s, before));
        h.redo(s, &size);
        QCOMPARE(size, crop.size());
        QCOMPARE(at(s, 10, 5), QColor(Qt::red));
    }

    // --- Lasso and feathering ---

    void polygonSelectsPixelsWhoseCentresAreInside()
    {
        // A right triangle with its corner at (10, 10) and legs of 20.
        const QPolygonF tri{QPointF(10, 10), QPointF(30, 10), QPointF(10, 30)};
        const Selection s = Selection::polygon(tri, kCanvas);
        QCOMPARE(s.shape(), Selection::Shape::Mask);
        QVERIFY(s.contains(11, 11));
        QVERIFY(s.contains(25, 12));
        QVERIFY(!s.contains(28, 28)); // beyond the long side
        QVERIFY(!s.contains(9, 15));
        QVERIFY(!s.isSoft());
        QVERIFY(s.bounds().width() <= 20 && s.bounds().height() <= 20);
        // About half the square.
        int n = 0;
        for (int y = 10; y < 30; ++y)
            for (int x = 10; x < 30; ++x)
                n += s.contains(x, y);
        QVERIFY(qAbs(n - 200) <= 22);

        // Clipped to the canvas; too few points or nothing inside is no selection.
        const Selection edge = Selection::polygon(
            QPolygonF{QPointF(-50, -50), QPointF(50, -50), QPointF(50, 50), QPointF(-50, 50)}, kCanvas);
        QCOMPARE(edge.bounds(), QRect(0, 0, 50, 50));
        QVERIFY(Selection::polygon(QPolygonF{QPointF(1, 1), QPointF(5, 5)}, kCanvas).isEmpty());
        QVERIFY(Selection::polygon(tri.translated(1000, 1000), kCanvas).isEmpty());
    }

    void featherFadesTheEdge()
    {
        const Selection hard = Selection::rect(QRect(100, 100, 100, 100));
        QCOMPARE(hard.coverage(150, 150), 1.0f);
        QCOMPARE(hard.coverage(99, 150), 0.0f);
        const Selection soft = hard.feathered(10, kCanvas);
        QVERIFY(soft.isSoft());
        QVERIFY(soft.coverage(150, 150) > 0.99f);        // the middle is untouched
        QVERIFY(qAbs(soft.coverage(100, 150) - 0.5f) < 0.06f); // the old edge is half in
        QVERIFY(qAbs(soft.coverage(99, 150) - 0.5f) < 0.06f);
        QVERIFY(soft.coverage(108, 150) > soft.coverage(102, 150)); // rising inward
        QVERIFY(soft.coverage(92, 150) > 0.0f && soft.coverage(92, 150) < 0.3f);
        QCOMPARE(soft.coverage(70, 150), 0.0f);          // and it ends
        QVERIFY(soft.bounds().contains(hard.bounds()));
        // Marching ants follow the half-covered line, where the edge was.
        qreal left = 1e9;
        for (const QPolygonF &line : soft.outlines())
            for (const QPointF &p : line)
                left = qMin(left, p.x());
        QVERIFY(qAbs(left - 100.0) <= 1.0);

        // Against the canvas edge the fade stops at the edge.
        const Selection corner = Selection::rect(QRect(0, 0, 50, 50)).feathered(10, kCanvas);
        QCOMPARE(corner.bounds().topLeft(), QPoint(0, 0));
        // Grow and shrink take the half-covered edge.
        QCOMPARE(soft.grown(1, kCanvas).bounds(), QRect(99, 99, 102, 102));
    }

    void softSelectionsCombineByCoverage()
    {
        const Selection soft = Selection::rect(QRect(100, 100, 100, 100)).feathered(10, kCanvas);
        const float edge = soft.coverage(100, 150);
        const Selection inv = soft.inverted(kCanvas);
        QVERIFY(qAbs(inv.coverage(100, 150) - (1.0f - edge)) < 0.01f);
        QCOMPARE(inv.coverage(150, 150) < 0.01f, true);
        QCOMPARE(inv.coverage(10, 10), 1.0f);
        const Selection both = soft.united(Selection::rect(QRect(90, 140, 20, 20)));
        QCOMPARE(both.coverage(100, 150), 1.0f);
        QVERIFY(qAbs(both.coverage(100, 120) - soft.coverage(100, 120)) < 0.01f);
    }

    void editsThroughAFeatheredSelectionArePartial()
    {
        TileStore s(Qt::white);
        s.fillRect(kCanvas, Qt::red);
        const Selection soft = Selection::rect(QRect(100, 100, 100, 100)).feathered(10, kCanvas);
        const float k = soft.coverage(100, 150);
        QVERIFY(k > 0.4f && k < 0.6f);

        // Copy takes that share of the pixel; delete leaves the rest.
        const QImage cut = extractSelection(s, soft);
        const auto *row = reinterpret_cast<const Pixel *>(cut.constScanLine(150 - soft.bounds().top()));
        QVERIFY(qAbs(float(row[100 - soft.bounds().left()].a) - k) < 0.01f);
        QVERIFY(qAbs(float(row[150 - soft.bounds().left()].a) - 1.0f) < 0.01f);
        clearSelection(s, soft, kCanvas);
        QVERIFY(qAbs(float(s.pixel(100, 150).a) - (1.0f - k)) < 0.01f);
        QVERIFY(float(s.pixel(150, 150).a) < 0.01f);
        QCOMPARE(float(s.pixel(50, 150).a), 1.0f);

        // A stroke through it fades the same way.
        TileStore p(Qt::white);
        BrushSettings b;
        b.size = 40;
        b.hardness = 1.0;
        BrushStroke stroke;
        stroke.begin(&p, kCanvas, b, Qt::black, BrushMode::Paint, {{60, 150}, 1.0}, soft);
        for (int x = 70; x <= 160; x += 5)
            stroke.moveTo({{double(x), 150}, 1.0});
        stroke.end();
        QCOMPARE(at(p, 75, 150), QColor(Qt::white));  // outside the fade: untouched
        QCOMPARE(at(p, 150, 150), QColor(Qt::black)); // fully inside
        const float mid = float(p.pixel(100, 150).r);  // linear: white x (1 - k)
        QVERIFY(qAbs(mid - (1.0f - k)) < 0.02f);
    }
};

QTEST_GUILESS_MAIN(TestSelection)
#include "tst_selection.moc"
