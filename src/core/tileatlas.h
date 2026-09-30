#pragma once

#include "tilepyramid.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QRectF>

#include <array>

namespace easel {

struct AtlasSlot {
    int page = -1;
    int index = -1;

    bool isValid() const { return page >= 0 && index >= 0; }
    friend bool operator==(AtlasSlot a, AtlasSlot b) { return a.page == b.page && a.index == b.index; }
    friend bool operator!=(AtlasSlot a, AtlasSlot b) { return !(a == b); }
};

// Book-keeping for GPU texture pages that hold pyramid tiles. No GPU calls here:
// the renderer creates a texture per page and uploads into the slots this assigns.
//
// Page 0, slot 0 is reserved for the store's default pixel, so tiles that don't
// exist are drawn from it. Least-recently-used tiles are evicted when all pages
// are full, but never a tile already used in the current frame.
class TileAtlas
{
public:
    static constexpr int PageSize = 2048;
    static constexpr int SlotsPerRow = PageSize / TileStore::TileSize;
    static constexpr int SlotsPerPage = SlotsPerRow * SlotsPerRow;
    static constexpr qint64 BytesPerPage = qint64(PageSize) * PageSize * 8; // RGBA16F

    explicit TileAtlas(int maxPages = 8);

    void reset();

    int maxPages() const { return m_maxPages; }
    int pageCount() const { return m_pageCount; }
    qsizetype residentCount() const { return m_entries.size(); }

    static AtlasSlot defaultSlot() { return {0, 0}; }
    // Creates page 0 (which holds the default slot) if no page exists yet.
    void ensureFirstPage();

    AtlasSlot find(const LevelTile &t) const;
    void touch(const LevelTile &t, quint64 frame);
    // Assigns a slot, evicting older tiles if needed. Invalid when every slot
    // is already in use this frame.
    AtlasSlot allocate(const LevelTile &t, quint64 frame);
    void release(const LevelTile &t);

    static QPoint slotOrigin(AtlasSlot s);
    // Normalised texture rectangle for a sub-rectangle of a slot, in texels.
    static QRectF uvRect(AtlasSlot s, const QRectF &texels);
    // Whole slot inset by half a texel, for clamping bilinear lookups.
    static QRectF clampRect(AtlasSlot s);

private:
    struct Entry {
        AtlasSlot slot;
        quint64 lastUsed = 0;
    };

    bool evictOlderThan(quint64 frame);
    void addPage();

    int m_maxPages;
    int m_pageCount = 0;
    QHash<LevelTile, Entry> m_entries;
    QList<AtlasSlot> m_free;
};

// One drawn quad: where on the canvas, and where in the atlas page.
struct TileInstance {
    std::array<float, 4> dst;   // canvas x, y, w, h
    std::array<float, 4> uv;    // u0, v0, u1, v1
    std::array<float, 4> clamp; // umin, vmin, umax, vmax
};
static_assert(sizeof(TileInstance) == 48);

struct TileUpload {
    LevelTile tile;
    AtlasSlot slot;
    QImage image; // TileStore::TileFormat
};

struct FramePlan {
    int level = 0;
    QList<QList<TileInstance>> pages; // instances grouped by atlas page
    QList<TileUpload> uploads;
    int instanceCount = 0;
    int fallbackCount = 0; // tiles drawn from a coarser ancestor this frame
    bool complete() const { return fallbackCount == 0; }
};

// Decides what to draw for the visible part of the canvas at one pyramid level.
// Tiles not yet on the GPU are uploaded up to the budget; the rest are drawn
// from their nearest resident ancestor, so nothing is ever blank.
FramePlan planFrame(const TilePyramid &pyramid, TileAtlas &atlas, const QRect &visibleCanvas,
                    int level, quint64 frame, int uploadBudget);

} // namespace easel
