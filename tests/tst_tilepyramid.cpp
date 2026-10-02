#include "tilepyramid.h"

#include <QTest>

using namespace easeletch;

class TestTilePyramid : public QObject
{
    Q_OBJECT

private slots:
    void topLevelCoversCanvas()
    {
        QCOMPARE(TilePyramid::topLevelFor(QSize(64, 64)), 0);
        QCOMPARE(TilePyramid::topLevelFor(QSize(65, 10)), 1);
        QCOMPARE(TilePyramid::topLevelFor(QSize(8000, 8000)), 7);
        QCOMPARE(TilePyramid::topLevelFor(QSize(8192, 100)), 7);
        QCOMPARE(TilePyramid::topLevelFor(QSize(8193, 100)), 8);
    }

    void levelForZoom()
    {
        QCOMPARE(TilePyramid::levelForZoom(4.0, 7), 0);
        QCOMPARE(TilePyramid::levelForZoom(1.0, 7), 0);
        QCOMPARE(TilePyramid::levelForZoom(0.75, 7), 0);
        QCOMPARE(TilePyramid::levelForZoom(0.5, 7), 1);
        QCOMPARE(TilePyramid::levelForZoom(0.3, 7), 1);
        QCOMPARE(TilePyramid::levelForZoom(0.25, 7), 2);
        QCOMPARE(TilePyramid::levelForZoom(0.001, 7), 7);
    }

    void ancestorsAndRects()
    {
        const LevelTile t{0, {5, -3}};
        QCOMPARE(TilePyramid::ancestor(t, 1), (LevelTile{1, {2, -2}}));
        QCOMPARE(TilePyramid::ancestor(t, 3), (LevelTile{3, {0, -1}}));
        QCOMPARE(TilePyramid::canvasRect({2, {1, 0}}), QRect(256, 0, 256, 256));
        QCOMPARE(TilePyramid::tilesIntersecting(1, QRect(0, 0, 129, 128)).size(), 2);
    }

    void existencePropagatesUp()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(700, 10, 1, 1), Qt::black); // base tile (10, 0)
        TilePyramid p;
        p.setBase(&s, QSize(1000, 1000));
        QVERIFY(p.exists({0, {10, 0}}));
        QVERIFY(p.exists({1, {5, 0}}));
        QVERIFY(p.exists({3, {1, 0}}));
        QVERIFY(p.exists({p.topLevel(), {0, 0}}));
        QVERIFY(!p.exists({1, {4, 0}}));
        QVERIFY(p.tile({1, {4, 0}}).isNull());
    }

    void downsampleAveragesInLinearLight()
    {
        TileStore s;
        // Checkerboard of opaque black and white pixels over one base tile.
        QImage img(64, 64, QImage::Format_ARGB32);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                img.setPixel(x, y, ((x + y) & 1) ? 0xffffffff : 0xff000000);
        s.writeImage(img);

        TilePyramid p;
        p.setBase(&s, QSize(64, 64));
        QCOMPARE(p.topLevel(), 0);

        TilePyramid big;
        big.setBase(&s, QSize(128, 128));
        const QImage l1 = big.tile({1, {0, 0}});
        QVERIFY(!l1.isNull());

        // Top-left quadrant came from the checkerboard: linear 0.5, opaque.
        const auto *px = reinterpret_cast<const Pixel *>(l1.constScanLine(0));
        QVERIFY(qAbs(float(px[0].r) - 0.5f) < 0.001f);
        QVERIFY(qAbs(float(px[0].a) - 1.0f) < 0.001f);
        // 0.5 linear is about 188 in sRGB, not the gamma-wrong 128.
        QVERIFY(qAbs(pixelToColor(px[0]).red() - 188) <= 1);

        // Bottom-right quadrant had no base tile: default (transparent).
        const auto *br = reinterpret_cast<const Pixel *>(l1.constScanLine(63));
        QCOMPARE(float(br[63].a), 0.0f);
    }

    void invalidateDropsAncestors()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 64, 64), Qt::red);
        TilePyramid p;
        p.setBase(&s, QSize(1024, 1024));
        const LevelTile top{p.topLevel(), {0, 0}};
        const QImage before = p.tile(top);
        QVERIFY(!before.isNull());
        QVERIFY(p.cachedTileCount() > 0);

        s.fillRect(QRect(0, 0, 64, 64), Qt::blue);
        const auto changed = p.invalidate(s.takeDirty());
        QCOMPARE(changed.size(), p.topLevel() + 1);
        QVERIFY(changed.contains(top));

        const QImage after = p.tile(top);
        const auto *a = reinterpret_cast<const Pixel *>(before.constScanLine(0));
        const auto *b = reinterpret_cast<const Pixel *>(after.constScanLine(0));
        QVERIFY(float(a[0].r) > float(b[0].r));
        QVERIFY(float(a[0].b) < float(b[0].b));
    }

    void removingLastTileClearsExistence()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 64, 64), Qt::red);
        TilePyramid p;
        p.setBase(&s, QSize(512, 512));
        QVERIFY(p.exists({2, {0, 0}}));

        s.fillRect(QRect(0, 0, 64, 64), Qt::white); // frees the tile
        p.invalidate(s.takeDirty());
        QVERIFY(!p.exists({2, {0, 0}}));
    }
};

QTEST_GUILESS_MAIN(TestTilePyramid)
#include "tst_tilepyramid.moc"
