#include "regionops.h"

#include <algorithm>
#include <cstring>

namespace easel {

namespace {

constexpr int N = TileStore::TileSize;

const Pixel *tilePixels(const QImage &tile)
{
    return reinterpret_cast<const Pixel *>(tile.constBits());
}

Pixel *tilePixels(QImage &tile)
{
    return reinterpret_cast<Pixel *>(tile.bits());
}

QImage filledTile(const Pixel &p)
{
    QImage t(N, N, TileStore::TileFormat);
    Pixel *d = tilePixels(t);
    std::fill(d, d + N * N, p);
    return t;
}

// Premultiplied source-over.
Pixel over(const Pixel &src, const Pixel &dst)
{
    const float k = 1.0f - float(src.a);
    return makePixel(float(src.r) + float(dst.r) * k, float(src.g) + float(dst.g) * k,
                     float(src.b) + float(dst.b) * k, float(src.a) + float(dst.a) * k);
}

} // namespace

QImage extractSelection(const TileStore &store, const Selection &selection)
{
    const QRect b = selection.bounds();
    QImage out(b.size(), TileStore::TileFormat);
    out.fill(Qt::transparent);
    if (selection.isEmpty())
        return out;

    const Pixel def = store.defaultPixel();
    for (const TileCoord c : TileStore::tilesIntersecting(b)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & b;
        const QImage tile = store.tile(c);
        const Pixel *src = tile.isNull() ? nullptr : tilePixels(tile);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            auto *dst = reinterpret_cast<Pixel *>(out.scanLine(y - b.top()));
            for (int x = part.left(); x <= part.right(); ++x) {
                if (!selection.contains(x, y))
                    continue;
                dst[x - b.left()] = src ? src[(y - tr.top()) * N + (x - tr.left())] : def;
            }
        }
    }
    return out;
}

QHash<TileCoord, QImage> clearSelection(TileStore &store, const Selection &selection, const QRect &canvas)
{
    QHash<TileCoord, QImage> before;
    const QRect area = selection.bounds() & canvas;
    if (selection.isEmpty() || area.isEmpty())
        return before;

    const Pixel clear = makePixel(0, 0, 0, 0);
    for (const TileCoord c : TileStore::tilesIntersecting(area)) {
        before.insert(c, store.tile(c));
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & area;
        Pixel *d = tilePixels(store.writableTile(c));
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                if (selection.contains(x, y))
                    d[(y - tr.top()) * N + (x - tr.left())] = clear;
    }
    return before;
}

QHash<TileCoord, QImage> cropStore(TileStore &store, const QRect &rect)
{
    QHash<TileCoord, QImage> before;
    if (rect.isEmpty())
        return before;
    const TileStore old = store.snapshot();
    const Pixel def = old.defaultPixel();
    const QRect canvas(QPoint(0, 0), rect.size());
    const QPoint shift = rect.topLeft();

    QHash<TileCoord, QImage> fresh;
    for (const TileCoord c : TileStore::tilesIntersecting(canvas)) {
        const QRect dst = TileStore::tileRect(c) & canvas;
        const QRect src = dst.translated(shift);
        const QList<TileCoord> sources = TileStore::tilesIntersecting(src);
        const bool any = std::any_of(sources.cbegin(), sources.cend(),
                                     [&](TileCoord s) { return old.hasTile(s); });
        if (!any)
            continue; // all default: leave the tile absent
        QImage tile = filledTile(def);
        Pixel *d = tilePixels(tile);
        const QRect tr = TileStore::tileRect(c);
        for (const TileCoord sc : sources) {
            const QImage s = old.tile(sc);
            if (s.isNull())
                continue; // default, already filled
            const QRect str = TileStore::tileRect(sc);
            const QRect part = str & src; // in old canvas coordinates
            const Pixel *sp = tilePixels(s);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const int dy = y - shift.y() - tr.top();
                const int dx = part.left() - shift.x() - tr.left();
                std::memcpy(d + dy * N + dx, sp + (y - str.top()) * N + (part.left() - str.left()),
                            size_t(part.width()) * sizeof(Pixel));
            }
        }
        fresh.insert(c, tile);
    }

    for (const TileCoord c : old.tileCoords())
        before.insert(c, old.tile(c));
    for (auto it = fresh.cbegin(); it != fresh.cend(); ++it)
        if (!before.contains(it.key()))
            before.insert(it.key(), QImage());
    for (auto it = before.cbegin(); it != before.cend(); ++it)
        store.setTile(it.key(), fresh.value(it.key()));
    return before;
}

QImage toClipboardImage(const QImage &content)
{
    QImage out(content.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < content.height(); ++y) {
        const auto *s = reinterpret_cast<const Pixel *>(content.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < content.width(); ++x)
            d[x] = pixelToDisplay(s[x]);
    }
    return out.convertToFormat(QImage::Format_ARGB32);
}

QImage fromClipboardImage(const QImage &image)
{
    const QImage src = image.convertToFormat(QImage::Format_RGBA8888);
    QImage out(src.size(), TileStore::TileFormat);
    for (int y = 0; y < src.height(); ++y) {
        const uchar *s = src.constScanLine(y);
        auto *d = reinterpret_cast<Pixel *>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x, s += 4) {
            const float a = float(s[3]) / 255.0f;
            d[x] = makePixel(srgb8ToLinear(s[0]) * a, srgb8ToLinear(s[1]) * a, srgb8ToLinear(s[2]) * a, a);
        }
    }
    return out;
}

// --- FloatingContent ----------------------------------------------------------

void FloatingContent::lift(TileStore *target, const Selection &selection, const QRect &canvas)
{
    reset();
    if (!target || selection.isEmpty())
        return;
    m_target = target;
    m_canvas = canvas;
    m_original = target->snapshot();
    m_content = extractSelection(m_original, selection);
    const QHash<TileCoord, QImage> cleared = clearSelection(*target, selection, canvas);
    for (auto it = cleared.cbegin(); it != cleared.cend(); ++it)
        m_touched.insert(it.key());
    m_base = target->snapshot(); // the canvas with the hole
    m_position = selection.bounds().topLeft();
    m_shape = selection.translated(-m_position);
    m_lifted = true;
    place();
}

void FloatingContent::paste(TileStore *target, const QImage &content, const QPoint &position,
                            const Selection &shape, const QRect &canvas)
{
    reset();
    if (!target || content.isNull())
        return;
    m_target = target;
    m_canvas = canvas;
    m_original = target->snapshot();
    m_base = m_original;
    m_content = content.format() == TileStore::TileFormat ? content : fromClipboardImage(content);
    m_position = position;
    m_shape = shape.isEmpty() ? Selection::rect(QRect(QPoint(0, 0), content.size())) : shape;
    m_lifted = false;
    place();
}

void FloatingContent::moveTo(const QPoint &position)
{
    if (!isActive() || position == m_position)
        return;
    m_position = position;
    place();
}

void FloatingContent::place()
{
    const QRect now = QRect(m_position, m_content.size()) & m_canvas;

    // Every tile covered before or now: rebuild from the base plus the content.
    QSet<TileCoord> tiles;
    for (const TileCoord c : TileStore::tilesIntersecting(m_placed))
        tiles.insert(c);
    for (const TileCoord c : TileStore::tilesIntersecting(now))
        tiles.insert(c);

    for (const TileCoord c : tiles) {
        m_touched.insert(c);
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & now;
        if (part.isEmpty()) {
            m_target->setTile(c, m_base.tile(c)); // uncovered again: back to the base
            continue;
        }
        QImage base = m_base.tile(c);
        QImage tile = base.isNull() ? filledTile(m_base.defaultPixel()) : base.copy();
        Pixel *d = tilePixels(tile);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const auto *s = reinterpret_cast<const Pixel *>(m_content.constScanLine(y - m_position.y()));
            for (int x = part.left(); x <= part.right(); ++x) {
                const Pixel &src = s[x - m_position.x()];
                if (float(src.a) <= 0.0f)
                    continue;
                Pixel &dst = d[(y - tr.top()) * N + (x - tr.left())];
                dst = over(src, dst);
            }
        }
        m_target->setTile(c, tile);
    }
    m_placed = now;
}

QHash<TileCoord, QImage> FloatingContent::commit()
{
    QHash<TileCoord, QImage> before;
    if (!isActive())
        return before;
    for (const TileCoord c : std::as_const(m_touched))
        before.insert(c, m_original.tile(c));
    reset();
    return before;
}

void FloatingContent::cancel()
{
    if (!isActive())
        return;
    for (const TileCoord c : std::as_const(m_touched))
        m_target->setTile(c, m_original.tile(c));
    reset();
}

void FloatingContent::reset()
{
    m_target = nullptr;
    m_original = TileStore();
    m_base = TileStore();
    m_content = QImage();
    m_shape = Selection();
    m_position = QPoint();
    m_placed = QRect();
    m_touched.clear();
    m_lifted = false;
}

} // namespace easel
