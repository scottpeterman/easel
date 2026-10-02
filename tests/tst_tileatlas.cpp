#include "tileatlas.h"

#include <QTest>

using namespace easeletch;

class TestTileAtlas : public QObject
{
    Q_OBJECT

private slots:
    void firstPageReservesDefaultSlot()
    {
        TileAtlas a(1);
        const AtlasSlot s = a.allocate({0, {0, 0}}, 1);
        QVERIFY(s.isValid());
        QVERIFY(s != TileAtlas::defaultSlot());
        QCOMPARE(a.pageCount(), 1);
        QCOMPARE(TileAtlas::slotOrigin({0, 33}), QPoint(64, 64));
    }

    void allocateIsIdempotent()
    {
        TileAtlas a;
        const LevelTile t{2, {3, 4}};
        const AtlasSlot s1 = a.allocate(t, 1);
        QCOMPARE(a.allocate(t, 2), s1);
        QCOMPARE(a.residentCount(), 1);
        a.release(t);
        QVERIFY(!a.find(t).isValid());
    }

    void evictsOnlyOlderFrames()
    {
        TileAtlas a(1);
        const int capacity = TileAtlas::SlotsPerPage - 1; // minus the default slot
        for (int i = 0; i < capacity; ++i)
            QVERIFY(a.allocate({0, {i, 0}}, 1).isValid());

        // Everything is in use this frame: no room.
        QVERIFY(!a.allocate({0, {-1, -1}}, 1).isValid());

        // Next frame, tile 0 is touched, so it must survive eviction.
        a.touch({0, {0, 0}}, 2);
        QVERIFY(a.allocate({0, {-1, -1}}, 2).isValid());
        QVERIFY(a.find({0, {0, 0}}).isValid());
        QVERIFY(a.residentCount() < capacity + 1);
        QCOMPARE(a.pageCount(), 1);
    }

    void uvRectsAreNormalised()
    {
        const QRectF uv = TileAtlas::uvRect({0, 1}, QRectF(0, 0, 64, 64));
        QCOMPARE(uv, QRectF(64.0 / 2048, 0, 64.0 / 2048, 64.0 / 2048));
        const QRectF cl = TileAtlas::clampRect({0, 0});
        QCOMPARE(cl.left(), 0.5 / 2048);
        QCOMPARE(cl.right(), 63.5 / 2048);
    }

    void planCoversVisibleArea()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 1000, 64), Qt::red); // 16 base tiles on row 0
        TilePyramid p;
        p.setBase(&s, QSize(1000, 1000));
        TileAtlas a;

        const FramePlan plan = planFrame(p, a, QRect(0, 0, 1000, 1000), 0, 1, 1000);
        QCOMPARE(plan.instanceCount, 16 * 16);
        QCOMPARE(plan.uploads.size(), 16);
        QVERIFY(plan.complete());

        // Second frame: everything resident, nothing to upload.
        const FramePlan again = planFrame(p, a, QRect(0, 0, 1000, 1000), 0, 2, 1000);
        QCOMPARE(again.uploads.size(), 0);
        QCOMPARE(again.instanceCount, 16 * 16);
    }

    void budgetFallsBackToAncestor()
    {
        TileStore s;
        s.fillRect(QRect(0, 0, 512, 512), Qt::blue); // 64 base tiles
        TilePyramid p;
        p.setBase(&s, QSize(512, 512));
        QCOMPARE(p.topLevel(), 3);
        TileAtlas a;

        const FramePlan plan = planFrame(p, a, QRect(0, 0, 512, 512), 0, 1, 10);
        QCOMPARE(plan.instanceCount, 64);
        QCOMPARE(plan.fallbackCount, 54);
        // 10 budgeted uploads plus the forced top-level tile.
        QCOMPARE(plan.uploads.size(), 11);
        QVERIFY(a.find({3, {0, 0}}).isValid());

        // Keep drawing: converges to full resolution.
        int frame = 2;
        FramePlan next;
        do {
            next = planFrame(p, a, QRect(0, 0, 512, 512), 0, quint64(frame++), 10);
        } while (!next.complete() && frame < 20);
        QVERIFY(next.complete());
        QCOMPARE(frame, 8);
    }

    void zoomedOutUsesFewTiles()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(4000, 4000, 300, 300), Qt::gray);
        TilePyramid p;
        p.setBase(&s, QSize(8000, 8000));
        TileAtlas a;

        const int level = TilePyramid::levelForZoom(0.1, p.topLevel());
        QCOMPARE(level, 3);
        const FramePlan plan = planFrame(p, a, QRect(0, 0, 8000, 8000), level, 1, 10000);
        QCOMPARE(plan.instanceCount, 16 * 16);
    }
};

QTEST_GUILESS_MAIN(TestTileAtlas)
#include "tst_tileatlas.moc"
