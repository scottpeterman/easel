#include "tilestore.h"

#include <algorithm>
#include <utility>

namespace easeletch {

namespace {

int floorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

Pixel *pixelLine(QImage &img, int y)
{
    return reinterpret_cast<Pixel *>(img.scanLine(y));
}

const Pixel *pixelLine(const QImage &img, int y)
{
    return reinterpret_cast<const Pixel *>(img.constScanLine(y));
}

void fillImage(QImage &img, const Pixel &px)
{
    for (int y = 0; y < img.height(); ++y) {
        Pixel *line = pixelLine(img, y);
        std::fill(line, line + img.width(), px);
    }
}

} // namespace

TileStore::TileStore(const QColor &defaultColor)
    : m_default(pixelFromColor(defaultColor))
{
}

TileCoord TileStore::tileAt(int px, int py)
{
    return {floorDiv(px, TileSize), floorDiv(py, TileSize)};
}

QRect TileStore::tileRect(TileCoord c)
{
    return QRect(c.x * TileSize, c.y * TileSize, TileSize, TileSize);
}

QList<TileCoord> TileStore::tilesIntersecting(const QRect &rect)
{
    QList<TileCoord> out;
    if (rect.isEmpty())
        return out;
    const TileCoord a = tileAt(rect.left(), rect.top());
    const TileCoord b = tileAt(rect.right(), rect.bottom());
    out.reserve(qsizetype(b.x - a.x + 1) * (b.y - a.y + 1));
    for (int ty = a.y; ty <= b.y; ++ty)
        for (int tx = a.x; tx <= b.x; ++tx)
            out.append({tx, ty});
    return out;
}

QImage &TileStore::writableTile(TileCoord c)
{
    auto it = m_tiles.find(c);
    if (it == m_tiles.end()) {
        QImage img(TileSize, TileSize, TileFormat);
        fillImage(img, m_default);
        it = m_tiles.insert(c, img);
    }
    m_dirty.insert(c);
    return it.value();
}

void TileStore::setTile(TileCoord c, const QImage &tile)
{
    if (tile.isNull()) {
        if (m_tiles.remove(c) > 0)
            m_dirty.insert(c);
        return;
    }
    Q_ASSERT(tile.size() == QSize(TileSize, TileSize) && tile.format() == TileFormat);
    m_tiles.insert(c, tile);
    m_dirty.insert(c);
}

Pixel TileStore::pixel(int x, int y) const
{
    const TileCoord c = tileAt(x, y);
    const auto it = m_tiles.constFind(c);
    if (it == m_tiles.cend())
        return m_default;
    const QRect r = tileRect(c);
    return pixelLine(it.value(), y - r.top())[x - r.left()];
}

void TileStore::fillRect(const QRect &rect, const QColor &srgb)
{
    fillRect(rect, pixelFromColor(srgb));
}

void TileStore::fillRect(const QRect &rect, const Pixel &px)
{
    const QRect area = rect.normalized();
    const bool isDefault = samePixel(px, m_default);

    for (const TileCoord c : tilesIntersecting(area)) {
        const QRect tr = tileRect(c);
        const QRect part = tr & area;

        // A tile fully covered by the default pixel is the same as no tile.
        if (isDefault && part == tr) {
            if (m_tiles.remove(c) > 0)
                m_dirty.insert(c);
            continue;
        }
        if (isDefault && !m_tiles.contains(c))
            continue;

        QImage &img = writableTile(c);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            Pixel *line = pixelLine(img, y - tr.top());
            std::fill(line + (part.left() - tr.left()), line + (part.right() - tr.left() + 1), px);
        }
    }
}

void TileStore::writeImage(const QImage &image, const QPoint &offset)
{
    if (image.isNull())
        return;

    // Source is treated as sRGB, straight alpha.
    const QImage src = image.convertToFormat(QImage::Format_RGBA8888);
    const QRect area(offset, src.size());

    for (const TileCoord c : tilesIntersecting(area)) {
        const QRect tr = tileRect(c);
        const QRect part = tr & area;
        QImage &img = writableTile(c);

        for (int y = part.top(); y <= part.bottom(); ++y) {
            const uchar *s = src.constScanLine(y - offset.y()) + (part.left() - offset.x()) * 4;
            Pixel *d = pixelLine(img, y - tr.top()) + (part.left() - tr.left());
            for (int x = part.left(); x <= part.right(); ++x, s += 4, ++d) {
                const float a = float(s[3]) / 255.0f;
                *d = makePixel(srgb8ToLinear(s[0]) * a, srgb8ToLinear(s[1]) * a,
                               srgb8ToLinear(s[2]) * a, a);
            }
        }
    }
}

void TileStore::clear()
{
    for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it)
        m_dirty.insert(it.key());
    m_tiles.clear();
}

QSet<TileCoord> TileStore::takeDirty()
{
    return std::exchange(m_dirty, {});
}

QImage TileStore::toDisplay(const QImage &tile)
{
    QImage out(tile.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tile.height(); ++y) {
        const Pixel *s = pixelLine(tile, y);
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < tile.width(); ++x)
            d[x] = pixelToDisplay(s[x]);
    }
    return out;
}

} // namespace easeletch
