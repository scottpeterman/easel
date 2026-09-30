#pragma once

#include "color.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>

namespace easel {

struct TileCoord {
    int x = 0;
    int y = 0;

    friend bool operator==(TileCoord a, TileCoord b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(TileCoord a, TileCoord b) { return !(a == b); }
};

inline size_t qHash(TileCoord c, size_t seed = 0) noexcept
{
    return ::qHash((quint64(quint32(c.x)) << 32) | quint32(c.y), seed);
}

// Sparse, unbounded tile storage for one raster layer.
//
// Tiles are 64x64 QImages in RGBA16F premultiplied, linear light. A tile that was
// never written reads as the store's default pixel and costs no memory.
//
// Copies are cheap: QHash and QImage are implicitly shared, so copying a store
// (snapshot()) shares every tile. The first write to a shared tile detaches only
// that tile. Undo builds on this.
class TileStore
{
public:
    static constexpr int TileSize = 64;
    static constexpr QImage::Format TileFormat = QImage::Format_RGBA16FPx4_Premultiplied;
    static constexpr qint64 BytesPerTile = qint64(TileSize) * TileSize * qint64(sizeof(Pixel));

    explicit TileStore(const QColor &defaultColor = QColor(0, 0, 0, 0));

    Pixel defaultPixel() const { return m_default; }
    QColor defaultColor() const { return pixelToColor(m_default); }

    bool hasTile(TileCoord c) const { return m_tiles.contains(c); }
    QImage tile(TileCoord c) const { return m_tiles.value(c); }
    QList<TileCoord> tileCoords() const { return m_tiles.keys(); }
    qsizetype tileCount() const { return m_tiles.size(); }
    qint64 memoryBytes() const { return qint64(m_tiles.size()) * BytesPerTile; }

    Pixel pixel(int x, int y) const;

    void fillRect(const QRect &rect, const QColor &srgb);
    void fillRect(const QRect &rect, const Pixel &pixel);
    void writeImage(const QImage &image, const QPoint &offset = QPoint());
    void clear();

    // Tile for writing: created from the default pixel if absent, detached from
    // any snapshot sharing it, and marked dirty. Its 64 rows are contiguous.
    QImage &writableTile(TileCoord c);
    // Replaces a whole tile; a null image removes it (reads as default again).
    void setTile(TileCoord c, const QImage &tile);

    QSet<TileCoord> takeDirty();
    bool hasDirty() const { return !m_dirty.isEmpty(); }

    TileStore snapshot() const { return *this; }

    static TileCoord tileAt(int px, int py);
    static QRect tileRect(TileCoord c);
    static QList<TileCoord> tilesIntersecting(const QRect &rect);

    // Converts a tile to premultiplied 8-bit sRGB for on-screen drawing.
    static QImage toDisplay(const QImage &tile);

private:
    Pixel m_default;
    QHash<TileCoord, QImage> m_tiles;
    QSet<TileCoord> m_dirty;
};

} // namespace easel
