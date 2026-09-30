#include "brush.h"
#include "history.h"

#include <QTest>

#include <cstring>

using namespace easel;

namespace {

BrushSettings hard(double size)
{
    BrushSettings s;
    s.size = size;
    s.hardness = 1.0;
    s.pressureSize = false;
    return s;
}

const QRect kBounds(0, 0, 4000, 4000);

// Paints one stroke and records it in the history, the way the app does.
void strokeAndRecord(TileStore &s, History &h, QPointF from, QPointF to, const QColor &c)
{
    BrushStroke stroke;
    stroke.begin(&s, kBounds, hard(30), c, BrushMode::Paint, {from, 1.0});
    stroke.moveTo({to, 1.0});
    h.push(QStringLiteral("Brush"), stroke.end());
}

bool same(const TileStore &a, const TileStore &b)
{
    auto keys = a.tileCoords();
    if (keys.size() != b.tileCount())
        return false;
    for (const TileCoord c : keys) {
        if (!b.hasTile(c))
            return false;
        const QImage x = a.tile(c), y = b.tile(c);
        if (std::memcmp(x.constBits(), y.constBits(), size_t(x.sizeInBytes())) != 0)
            return false;
    }
    return true;
}

} // namespace

class TestHistory : public QObject
{
    Q_OBJECT

private slots:
    void undoRestoresExactlyAndRedoReapplies()
    {
        TileStore s(Qt::white);
        History h;
        h.reset(QStringLiteral("New"));

        const TileStore blank = s.snapshot();
        strokeAndRecord(s, h, {10, 10}, {200, 10}, Qt::red);
        const TileStore afterOne = s.snapshot();
        strokeAndRecord(s, h, {10, 50}, {200, 50}, Qt::blue);
        const TileStore afterTwo = s.snapshot();
        QCOMPARE(h.count(), 2);
        QCOMPARE(h.undoLabel(), QStringLiteral("Brush"));

        h.undo(s);
        QVERIFY(same(s, afterOne));
        h.undo(s);
        QVERIFY(same(s, blank));
        QCOMPARE(s.tileCount(), 0); // tiles that didn't exist are gone again
        QVERIFY(!h.canUndo());

        h.redo(s);
        h.redo(s);
        QVERIFY(same(s, afterTwo));
        QVERIFY(!h.canRedo());
    }

    void undoMarksTilesDirty()
    {
        TileStore s(Qt::white);
        History h;
        strokeAndRecord(s, h, {10, 10}, {100, 10}, Qt::red);
        s.takeDirty();
        h.undo(s);
        QCOMPARE(s.takeDirty().size(), 2); // x 0..115 spans tiles 0 and 1
    }

    void newActionDiscardsRedo()
    {
        TileStore s(Qt::white);
        History h;
        strokeAndRecord(s, h, {10, 10}, {50, 10}, Qt::red);
        strokeAndRecord(s, h, {10, 30}, {50, 30}, Qt::red);
        h.undo(s);
        QVERIFY(h.canRedo());
        strokeAndRecord(s, h, {10, 60}, {50, 60}, Qt::green);
        QVERIFY(!h.canRedo());
        QCOMPARE(h.count(), 2);
    }

    void jumpToMovesAcrossManySteps()
    {
        TileStore s(Qt::white);
        History h;
        const TileStore blank = s.snapshot();
        for (int i = 0; i < 10; ++i)
            strokeAndRecord(s, h, {10, 10.0 + i * 20}, {300, 10.0 + i * 20}, Qt::black);
        const TileStore all = s.snapshot();
        h.jumpTo(0, s);
        QVERIFY(same(s, blank));
        h.jumpTo(10, s);
        QVERIFY(same(s, all));
        QCOMPARE(h.position(), 10);
    }

    void memoryCountsOneCopyOfChangedTiles()
    {
        TileStore s(QColor(0, 0, 0, 0));
        s.fillRect(QRect(0, 0, 256, 64), Qt::gray); // 4 existing tiles
        s.takeDirty();
        History h;
        strokeAndRecord(s, h, {10, 30}, {240, 30}, Qt::red); // touches those 4
        QCOMPARE(h.memoryBytes(), 4 * TileStore::BytesPerTile);
        h.undo(s); // the entry now holds the after-state: same size
        QCOMPARE(h.memoryBytes(), 4 * TileStore::BytesPerTile);
    }

    void stateIdTracksExactPointInHistory()
    {
        TileStore s(Qt::white);
        History h;
        h.reset(QStringLiteral("New"));
        const quint64 base = h.stateId();
        strokeAndRecord(s, h, {10, 10}, {60, 10}, Qt::red);
        const quint64 saved = h.stateId();
        QVERIFY(saved != base);

        h.undo(s);
        QCOMPARE(h.stateId(), base);
        h.redo(s);
        QCOMPARE(h.stateId(), saved);

        // Undo past the save and paint something else: same step count,
        // different state.
        h.undo(s);
        strokeAndRecord(s, h, {10, 40}, {60, 40}, Qt::blue);
        QCOMPARE(h.position(), 1);
        QVERIFY(h.stateId() != saved);

        h.reset(QStringLiteral("Other"));
        QVERIFY(h.stateId() != base); // a new document never matches an old state
    }

    void droppingOldEntriesKeepsCurrentStateId()
    {
        TileStore s(QColor(0, 0, 0, 0));
        s.fillRect(QRect(0, 0, 64, 64), Qt::gray);
        History h(2 * TileStore::BytesPerTile);
        strokeAndRecord(s, h, {20, 20}, {30, 20}, Qt::red);
        strokeAndRecord(s, h, {20, 20}, {30, 20}, Qt::red);
        const quint64 current = h.stateId();
        strokeAndRecord(s, h, {20, 20}, {30, 20}, Qt::red); // drops the first
        QVERIFY(h.droppedCount() >= 1);
        h.undo(s);
        QCOMPARE(h.stateId(), current);
    }

    void budgetDropsOldestEntries()
    {
        TileStore s(QColor(0, 0, 0, 0));
        s.fillRect(QRect(0, 0, 64, 64), Qt::gray);
        History h(3 * TileStore::BytesPerTile); // room for three one-tile entries
        for (int i = 0; i < 10; ++i)
            strokeAndRecord(s, h, {20, 20}, {30, 20}, Qt::red);
        QVERIFY(h.memoryBytes() <= h.budget());
        QCOMPARE(h.count(), 3);
        QCOMPARE(h.droppedCount(), 7);
        QCOMPARE(h.position(), 3);
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
