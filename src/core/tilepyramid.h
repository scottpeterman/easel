#pragma once

#include "tilestore.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSet>
#include <QSize>

namespace easel {

struct LevelTile {
    int level = 0;
    TileCoord coord;

    friend bool operator==(const LevelTile &a, const LevelTile &b)
    {
        return a.level == b.level && a.coord == b.coord;
    }
    friend bool operator!=(const LevelTile &a, const LevelTile &b) { return !(a == b); }
};

inline size_t qHash(const LevelTile &t, size_t seed = 0) noexcept
{
    return qHash(t.coord, seed ^ (size_t(t.level) * 0x9e3779b97f4a7c15ULL));
}

// Mip pyramid over a TileStore, for drawing zoomed-out views.
//
// Level 0 is the store itself. A level-L tile is 64x64 pixels covering
// (64 << L) canvas pixels per side, box-filtered 2x2 from level L-1 in linear,
// premultiplied space. Levels are built lazily and cached; invalidate() drops
// the ancestors of changed base tiles.
//
// A tile "exists" at a level when any base tile under it exists. Tiles that
// don't exist read as the store's default pixel and need no drawing data.
class TilePyramid
{
public:
    TilePyramid() = default;

    void setBase(const TileStore *store, const QSize &canvasSize);
    const TileStore *base() const { return m_base; }

    // Levels 0..topLevel(); the top level covers the canvas in a single tile.
    int topLevel() const { return m_topLevel; }
    static int topLevelFor(const QSize &canvasSize);

    // Coarsest level whose texels are still no smaller than a screen pixel.
    static int levelForZoom(double zoom, int topLevel);

    // Canvas-pixel rectangle covered by a tile at a level.
    static QRect canvasRect(const LevelTile &t);
    // Tiles at a level intersecting a canvas-pixel rectangle.
    static QList<TileCoord> tilesIntersecting(int level, const QRect &canvasRect);
    // Ancestor of a tile, n levels up.
    static LevelTile ancestor(const LevelTile &t, int levelsUp);

    bool exists(const LevelTile &t) const;
    // Pixel data (TileStore::TileFormat). Null when the tile doesn't exist.
    QImage tile(const LevelTile &t) const;

    // Drops cached levels above the given base tiles. Returns every pyramid
    // tile whose content may have changed, including the base tiles.
    QList<LevelTile> invalidate(const QSet<TileCoord> &changedBaseTiles);
    void clearCache();

    // Computes every level up front. For a freshly opened image, so the first
    // zoomed-out frame doesn't build the whole pyramid on the UI thread.
    void buildAll();

    qsizetype cachedTileCount() const;

private:
    QImage downsample(const LevelTile &t) const;

    const TileStore *m_base = nullptr;
    QSize m_canvasSize;
    int m_topLevel = 0;

    // Levels >= 1. A null image caches "does not exist".
    mutable QHash<LevelTile, QImage> m_images;
    mutable QHash<LevelTile, bool> m_exists;
};

} // namespace easel
