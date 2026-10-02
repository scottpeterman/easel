#include "history.h"

#include <algorithm>

namespace easeletch {

History::History(qint64 budgetBytes)
    : m_budget(std::max<qint64>(budgetBytes, 0))
{
    m_baseId = m_nextId++;
}

void History::reset(const QString &baseLabel)
{
    m_baseLabel = baseLabel;
    m_entries.clear();
    m_position = 0;
    m_bytes = 0;
    m_dropped = 0;
    m_baseId = m_nextId++;
}

quint64 History::stateId() const
{
    return m_position == 0 ? m_baseId : m_entries.at(m_position - 1).id;
}

qint64 History::bytesOf(const QHash<TileCoord, QImage> &tiles)
{
    qint64 n = 0;
    for (auto it = tiles.cbegin(); it != tiles.cend(); ++it)
        if (!it.value().isNull())
            n += TileStore::BytesPerTile;
    return n;
}

void History::append(Entry e)
{
    while (m_entries.size() > m_position) {
        m_bytes -= m_entries.last().bytes;
        m_entries.removeLast();
    }
    e.id = m_nextId++;
    m_bytes += e.bytes;
    m_entries.append(std::move(e));
    m_position = m_entries.size();
    enforceBudget();
}

void History::push(const QString &label, QHash<TileCoord, QImage> tiles, const QSize &sizeBefore)
{
    Entry e;
    e.label = label;
    e.tiles = std::move(tiles);
    e.size = sizeBefore;
    e.bytes = bytesOf(e.tiles);
    append(std::move(e));
}

void History::push(const QString &label, int layerId, QHash<TileCoord, QImage> tiles, bool mask)
{
    Entry e;
    e.label = label;
    e.layerId = layerId;
    e.mask = mask;
    e.tiles = std::move(tiles);
    e.bytes = bytesOf(e.tiles);
    append(std::move(e));
}

void History::pushState(const QString &label, LayerStack before, const LayerStack &now)
{
    Entry e;
    e.label = label;
    e.state = std::make_shared<LayerStack>(std::move(before));
    e.bytes = e.state->bytesNotSharedWith(now);
    append(std::move(e));
}

bool History::swap(Entry &e, LayerStack &stack)
{
    if (e.state) {
        stack.swapState(*e.state);
        m_bytes -= e.bytes;
        e.bytes = e.state->bytesNotSharedWith(stack);
        m_bytes += e.bytes;
        return true;
    }
    if (Layer *l = stack.layer(e.layerId))
        swap(e, e.mask ? l->mask : l->store, nullptr);
    return false;
}

bool History::undo(LayerStack &stack)
{
    if (!canUndo())
        return false;
    --m_position;
    return swap(m_entries[m_position], stack);
}

bool History::redo(LayerStack &stack)
{
    if (!canRedo())
        return false;
    const bool changed = swap(m_entries[m_position], stack);
    ++m_position;
    return changed;
}

bool History::jumpTo(qsizetype position, LayerStack &stack)
{
    position = std::clamp<qsizetype>(position, 0, m_entries.size());
    bool changed = false;
    while (m_position > position)
        changed |= undo(stack);
    while (m_position < position)
        changed |= redo(stack);
    return changed;
}

void History::swap(Entry &e, TileStore &store, QSize *canvasSize)
{
    if (e.size.isValid() && canvasSize)
        std::swap(*canvasSize, e.size);
    for (auto it = e.tiles.begin(); it != e.tiles.end(); ++it) {
        const QImage current = store.tile(it.key());
        store.setTile(it.key(), it.value());
        it.value() = current;
    }
    m_bytes -= e.bytes;
    e.bytes = bytesOf(e.tiles);
    m_bytes += e.bytes;
}

void History::undo(TileStore &store, QSize *canvasSize)
{
    if (!canUndo())
        return;
    --m_position;
    swap(m_entries[m_position], store, canvasSize);
}

void History::redo(TileStore &store, QSize *canvasSize)
{
    if (!canRedo())
        return;
    swap(m_entries[m_position], store, canvasSize);
    ++m_position;
}

void History::jumpTo(qsizetype position, TileStore &store, QSize *canvasSize)
{
    position = std::clamp<qsizetype>(position, 0, m_entries.size());
    while (m_position > position)
        undo(store, canvasSize);
    while (m_position < position)
        redo(store, canvasSize);
}

void History::setBudget(qint64 bytes)
{
    m_budget = std::max<qint64>(bytes, 0);
    enforceBudget();
}

void History::enforceBudget()
{
    // Keep at least the newest entry, whatever its size.
    while (m_bytes > m_budget && m_position > 1) {
        m_bytes -= m_entries.first().bytes;
        m_baseId = m_entries.first().id; // the base state is now "after" it
        m_entries.removeFirst();
        --m_position;
        ++m_dropped;
    }
}

} // namespace easeletch
