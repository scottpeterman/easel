#include "tilepyramid.h"

#include <algorithm>
#include <cmath>

namespace easel {

namespace {

int floorShift(int v, int n)
{
    // Arithmetic right shift rounds toward negative infinity for signed ints.
    return v >> n;
}

const Pixel *pixelLine(const QImage &img, int y)
{
    return reinterpret_cast<const Pixel *>(img.constScanLine(y));
}

} // namespace

void TilePyramid::setBase(const TileStore *store, const QSize &canvasSize)
{
    m_base = store;
    m_canvasSize = canvasSize;
    m_topLevel = topLevelFor(canvasSize);
    clearCache();
}

int TilePyramid::topLevelFor(const QSize &canvasSize)
{
    const int extent = std::max({canvasSize.width(), canvasSize.height(), 1});
    int level = 0;
    while ((TileStore::TileSize << level) < extent && level < 24)
        ++level;
    return level;
}

int TilePyramid::levelForZoom(double zoom, int topLevel)
{
    if (!(zoom > 0.0) || zoom >= 1.0)
        return 0;
    const int level = int(std::floor(std::log2(1.0 / zoom) + 1e-9));
    return std::clamp(level, 0, topLevel);
}

QRect TilePyramid::canvasRect(const LevelTile &t)
{
    const int size = TileStore::TileSize << t.level;
    return QRect(t.coord.x * size, t.coord.y * size, size, size);
}

QList<TileCoord> TilePyramid::tilesIntersecting(int level, const QRect &canvasRect)
{
    QList<TileCoord> out;
    if (canvasRect.isEmpty())
        return out;
    const TileCoord a = TileStore::tileAt(canvasRect.left(), canvasRect.top());
    const TileCoord b = TileStore::tileAt(canvasRect.right(), canvasRect.bottom());
    const int x0 = floorShift(a.x, level), y0 = floorShift(a.y, level);
    const int x1 = floorShift(b.x, level), y1 = floorShift(b.y, level);
    out.reserve(qsizetype(x1 - x0 + 1) * (y1 - y0 + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            out.append({x, y});
    return out;
}

LevelTile TilePyramid::ancestor(const LevelTile &t, int levelsUp)
{
    return {t.level + levelsUp, {floorShift(t.coord.x, levelsUp), floorShift(t.coord.y, levelsUp)}};
}

bool TilePyramid::exists(const LevelTile &t) const
{
    if (!m_base)
        return false;
    if (t.level == 0)
        return m_base->hasTile(t.coord);

    const auto it = m_exists.constFind(t);
    if (it != m_exists.cend())
        return it.value();

    bool any = false;
    for (int dy = 0; dy < 2 && !any; ++dy)
        for (int dx = 0; dx < 2 && !any; ++dx)
            any = exists({t.level - 1, {t.coord.x * 2 + dx, t.coord.y * 2 + dy}});
    m_exists.insert(t, any);
    return any;
}

QImage TilePyramid::tile(const LevelTile &t) const
{
    if (!m_base)
        return {};
    if (t.level == 0)
        return m_base->tile(t.coord);

    const auto it = m_images.constFind(t);
    if (it != m_images.cend())
        return it.value();

    const QImage img = exists(t) ? downsample(t) : QImage();
    m_images.insert(t, img);
    return img;
}

QImage TilePyramid::downsample(const LevelTile &t) const
{
    constexpr int N = TileStore::TileSize;
    constexpr int H = N / 2;

    QImage out(N, N, TileStore::TileFormat);
    const Pixel def = m_base->defaultPixel();

    for (int qy = 0; qy < 2; ++qy) {
        for (int qx = 0; qx < 2; ++qx) {
            const LevelTile child{t.level - 1, {t.coord.x * 2 + qx, t.coord.y * 2 + qy}};
            const QImage src = exists(child) ? tile(child) : QImage();

            for (int y = 0; y < H; ++y) {
                auto *dst = reinterpret_cast<Pixel *>(out.scanLine(qy * H + y)) + qx * H;
                if (src.isNull()) {
                    std::fill(dst, dst + H, def);
                    continue;
                }
                const Pixel *r0 = pixelLine(src, y * 2);
                const Pixel *r1 = pixelLine(src, y * 2 + 1);
                for (int x = 0; x < H; ++x) {
                    const Pixel &a = r0[x * 2], &b = r0[x * 2 + 1];
                    const Pixel &c = r1[x * 2], &e = r1[x * 2 + 1];
                    dst[x] = makePixel(
                        (float(a.r) + float(b.r) + float(c.r) + float(e.r)) * 0.25f,
                        (float(a.g) + float(b.g) + float(c.g) + float(e.g)) * 0.25f,
                        (float(a.b) + float(b.b) + float(c.b) + float(e.b)) * 0.25f,
                        (float(a.a) + float(b.a) + float(c.a) + float(e.a)) * 0.25f);
                }
            }
        }
    }
    return out;
}

QList<LevelTile> TilePyramid::invalidate(const QSet<TileCoord> &changedBaseTiles)
{
    QSet<LevelTile> touched;
    for (const TileCoord c : changedBaseTiles) {
        touched.insert({0, c});
        for (int level = 1; level <= m_topLevel; ++level) {
            const LevelTile a = ancestor({0, c}, level);
            if (touched.contains(a))
                break; // Everything above is already queued.
            touched.insert(a);
            m_images.remove(a);
            m_exists.remove(a);
        }
    }
    return QList<LevelTile>(touched.cbegin(), touched.cend());
}

void TilePyramid::clearCache()
{
    m_images.clear();
    m_exists.clear();
}

qsizetype TilePyramid::cachedTileCount() const
{
    qsizetype n = 0;
    for (auto it = m_images.cbegin(); it != m_images.cend(); ++it)
        n += it.value().isNull() ? 0 : 1;
    return n;
}

} // namespace easel
