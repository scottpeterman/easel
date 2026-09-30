#pragma once

#include "tilestore.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

namespace easel {

// Undo history built on tile snapshots.
//
// Each entry keeps only the tiles an action changed. Undo and redo swap those
// tiles with the store's current ones, so an entry always holds "the other
// state" and costs one copy of the changed tiles, never the whole image.
// When the entries' memory exceeds the budget, the oldest are dropped.
class History
{
public:
    static constexpr qint64 DefaultBudget = qint64(1024) * 1024 * 1024; // 1 GiB

    explicit History(qint64 budgetBytes = DefaultBudget);

    // Starts over with a single, non-undoable base state ("New 2000 x 1500").
    void reset(const QString &baseLabel);

    // Records an action that has already been applied to the store.
    // tiles: the pre-action content of each changed tile (null = didn't exist).
    // Anything that was undone is discarded.
    void push(const QString &label, QHash<TileCoord, QImage> tiles);

    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position < m_entries.size(); }
    void undo(TileStore &store);
    void redo(TileStore &store);
    // Undoes or redoes until `position` entries are applied.
    void jumpTo(qsizetype position, TileStore &store);

    QString baseLabel() const { return m_baseLabel; }
    qsizetype count() const { return m_entries.size(); }
    qsizetype position() const { return m_position; }
    QString label(qsizetype index) const { return m_entries.at(index).label; }
    QString undoLabel() const { return canUndo() ? label(m_position - 1) : QString(); }
    QString redoLabel() const { return canRedo() ? label(m_position) : QString(); }

    qint64 budget() const { return m_budget; }
    void setBudget(qint64 bytes);
    qint64 memoryBytes() const { return m_bytes; }
    // Entries dropped from the front to stay within budget since reset().
    int droppedCount() const { return m_dropped; }

private:
    struct Entry {
        QString label;
        QHash<TileCoord, QImage> tiles;
        qint64 bytes = 0;
    };

    static qint64 bytesOf(const QHash<TileCoord, QImage> &tiles);
    void swap(Entry &e, TileStore &store);
    void enforceBudget();

    qint64 m_budget;
    QString m_baseLabel;
    QList<Entry> m_entries;
    qsizetype m_position = 0;
    qint64 m_bytes = 0;
    int m_dropped = 0;
};

} // namespace easel
