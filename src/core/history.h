#pragma once

#include "layerstack.h"
#include "tilestore.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <memory>

namespace easel {

// Undo history built on tile snapshots.
//
// Each entry keeps only the tiles an action changed. Undo and redo swap those
// tiles with the store's current ones, so an entry always holds "the other
// state" and costs one copy of the changed tiles, never the whole image.
// When the entries' memory exceeds the budget, the oldest are dropped.
//
// With layers, a tile entry names the layer it changed. Anything that changes
// the stack itself (add, delete, reorder, opacity, crop, ...) is recorded as a
// snapshot of the layers before it; snapshots share their tiles with the
// document, so they only cost what has changed since.
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
    // sizeBefore: the canvas size before the action, for actions that change
    // it (crop); invalid when the size didn't change.
    void push(const QString &label, QHash<TileCoord, QImage> tiles, const QSize &sizeBefore = {});
    // The same, for one layer of a stack.
    void push(const QString &label, int layerId, QHash<TileCoord, QImage> tiles);
    // Records a change to the stack that has already been made. before: a
    // snapshot() taken before it; now: the stack as it is.
    void pushState(const QString &label, LayerStack before, const LayerStack &now);

    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position < m_entries.size(); }
    // canvasSize, when given, is updated by entries that changed the size.
    void undo(TileStore &store, QSize *canvasSize = nullptr);
    void redo(TileStore &store, QSize *canvasSize = nullptr);
    // Undoes or redoes until `position` entries are applied.
    void jumpTo(qsizetype position, TileStore &store, QSize *canvasSize = nullptr);
    // For a layered document. True when the stack itself changed (layers
    // added, removed, reordered, resized ...), not just one layer's tiles:
    // the caller then recomposites everything.
    bool undo(LayerStack &stack);
    bool redo(LayerStack &stack);
    bool jumpTo(qsizetype position, LayerStack &stack);

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

    // Identifies the current document state. Two states have the same id only
    // if they're the same point in history, so "saved state == current state"
    // is exact: undoing past a save and painting something new is a change even
    // if the step count matches.
    quint64 stateId() const;

private:
    struct Entry {
        QString label;
        int layerId = 0; // the layer the tiles belong to
        QHash<TileCoord, QImage> tiles;
        std::shared_ptr<LayerStack> state; // the other state's layers, for stack changes
        QSize size; // the other state's canvas size, if it differs
        qint64 bytes = 0;
        quint64 id = 0;
    };

    static qint64 bytesOf(const QHash<TileCoord, QImage> &tiles);
    void swap(Entry &e, TileStore &store, QSize *canvasSize);
    bool swap(Entry &e, LayerStack &stack);
    void append(Entry e);
    void enforceBudget();

    qint64 m_budget;
    QString m_baseLabel;
    QList<Entry> m_entries;
    qsizetype m_position = 0;
    qint64 m_bytes = 0;
    int m_dropped = 0;
    quint64 m_nextId = 1;
    quint64 m_baseId = 0; // state before the first remaining entry
};

} // namespace easel
