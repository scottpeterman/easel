#include "tileatlas.h"

#include <algorithm>
#include <utility>

namespace easel {

TileAtlas::TileAtlas(int maxPages)
    : m_maxPages(std::max(1, maxPages))
{
    reset();
}

void TileAtlas::reset()
{
    m_entries.clear();
    m_free.clear();
    m_pageCount = 0;
}

AtlasSlot TileAtlas::find(const LevelTile &t) const
{
    const auto it = m_entries.constFind(t);
    return it == m_entries.cend() ? AtlasSlot{} : it->slot;
}

void TileAtlas::touch(const LevelTile &t, quint64 frame)
{
    const auto it = m_entries.find(t);
    if (it != m_entries.end())
        it->lastUsed = frame;
}

AtlasSlot TileAtlas::allocate(const LevelTile &t, quint64 frame)
{
    if (const AtlasSlot existing = find(t); existing.isValid()) {
        touch(t, frame);
        return existing;
    }

    if (m_free.isEmpty() && m_pageCount < m_maxPages)
        addPage();
    if (m_free.isEmpty() && !evictOlderThan(frame))
        return {};

    const AtlasSlot slot = m_free.takeLast();
    m_entries.insert(t, {slot, frame});
    return slot;
}

void TileAtlas::ensureFirstPage()
{
    if (m_pageCount == 0)
        addPage();
}

void TileAtlas::addPage()
{
    // Slots are handed out in ascending order; page 0 slot 0 is reserved.
    const int page = m_pageCount++;
    for (int i = SlotsPerPage - 1; i >= (page == 0 ? 1 : 0); --i)
        m_free.append({page, i});
}

void TileAtlas::release(const LevelTile &t)
{
    const auto it = m_entries.constFind(t);
    if (it == m_entries.cend())
        return;
    m_free.append(it->slot);
    m_entries.erase(it);
}

bool TileAtlas::evictOlderThan(quint64 frame)
{
    // Evict the oldest quarter of what's evictable, so eviction cost is amortised.
    QList<std::pair<quint64, LevelTile>> candidates;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (it->lastUsed < frame)
            candidates.append({it->lastUsed, it.key()});
    if (candidates.isEmpty())
        return false;

    const qsizetype n = std::max<qsizetype>(1, candidates.size() / 4);
    std::nth_element(candidates.begin(), candidates.begin() + (n - 1), candidates.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (qsizetype i = 0; i < n; ++i)
        release(candidates.at(i).second);
    return true;
}

QPoint TileAtlas::slotOrigin(AtlasSlot s)
{
    return QPoint((s.index % SlotsPerRow) * TileStore::TileSize,
                  (s.index / SlotsPerRow) * TileStore::TileSize);
}

QRectF TileAtlas::uvRect(AtlasSlot s, const QRectF &texels)
{
    const QPointF o = slotOrigin(s);
    const QRectF r = texels.translated(o);
    return QRectF(r.x() / PageSize, r.y() / PageSize, r.width() / PageSize, r.height() / PageSize);
}

QRectF TileAtlas::clampRect(AtlasSlot s)
{
    const qreal n = TileStore::TileSize;
    return uvRect(s, QRectF(0.5, 0.5, n - 1.0, n - 1.0));
}

namespace {

void appendInstance(FramePlan &plan, AtlasSlot slot, const QRect &dst, const QRectF &texels)
{
    const QRectF uv = TileAtlas::uvRect(slot, texels);
    const QRectF cl = TileAtlas::clampRect(slot);
    TileInstance inst;
    inst.dst = {float(dst.x()), float(dst.y()), float(dst.width()), float(dst.height())};
    inst.uv = {float(uv.left()), float(uv.top()), float(uv.right()), float(uv.bottom())};
    inst.clamp = {float(cl.left()), float(cl.top()), float(cl.right()), float(cl.bottom())};

    if (plan.pages.size() <= slot.page)
        plan.pages.resize(slot.page + 1);
    plan.pages[slot.page].append(inst);
    ++plan.instanceCount;
}

} // namespace

FramePlan planFrame(const TilePyramid &pyramid, TileAtlas &atlas, const QRect &visibleCanvas,
                    int level, quint64 frame, int uploadBudget)
{
    FramePlan plan;
    plan.level = std::clamp(level, 0, pyramid.topLevel());
    const qreal full = TileStore::TileSize;
    const QRectF wholeSlot(0, 0, full, full);

    // The default-pixel slot must exist before anything else is handed out.
    atlas.ensureFirstPage();

    for (const TileCoord c : TilePyramid::tilesIntersecting(plan.level, visibleCanvas)) {
        const LevelTile t{plan.level, c};
        const QRect dst = TilePyramid::canvasRect(t);

        if (!pyramid.exists(t)) {
            appendInstance(plan, TileAtlas::defaultSlot(), dst, wholeSlot);
            continue;
        }

        if (const AtlasSlot s = atlas.find(t); s.isValid()) {
            atlas.touch(t, frame);
            appendInstance(plan, s, dst, wholeSlot);
            continue;
        }

        if (uploadBudget > 0) {
            if (const AtlasSlot s = atlas.allocate(t, frame); s.isValid()) {
                plan.uploads.append({t, s, pyramid.tile(t)});
                --uploadBudget;
                appendInstance(plan, s, dst, wholeSlot);
                continue;
            }
        }

        // Not resident yet: draw the matching part of the nearest resident ancestor.
        // The top-level tile is always made resident, whatever the budget.
        ++plan.fallbackCount;
        bool drawn = false;
        for (int up = 1; t.level + up <= pyramid.topLevel(); ++up) {
            const LevelTile a = TilePyramid::ancestor(t, up);
            AtlasSlot s = atlas.find(a);
            if (!s.isValid() && a.level == pyramid.topLevel()) {
                s = atlas.allocate(a, frame);
                if (s.isValid())
                    plan.uploads.append({a, s, pyramid.tile(a)});
            }
            if (!s.isValid())
                continue;
            atlas.touch(a, frame);
            const qreal span = full / qreal(1 << up);
            const QRectF texels((c.x - (a.coord.x << up)) * span, (c.y - (a.coord.y << up)) * span,
                                span, span);
            appendInstance(plan, s, dst, texels);
            drawn = true;
            break;
        }
        if (!drawn)
            appendInstance(plan, TileAtlas::defaultSlot(), dst, wholeSlot);
    }
    return plan;
}

} // namespace easel
