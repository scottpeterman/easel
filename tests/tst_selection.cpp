#include "regionops.h"

#include <QTest>

#include <cstring>

using namespace easel;

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
};

QTEST_GUILESS_MAIN(TestSelection)
#include "tst_selection.moc"
