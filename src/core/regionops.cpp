#include "regionops.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

namespace easeletch {

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
                const float k = selection.coverage(x, y);
                if (k <= 0.0f)
                    continue;
                const Pixel &p = src ? src[(y - tr.top()) * N + (x - tr.left())] : def;
                dst[x - b.left()] = k >= 1.0f ? p : makePixel(float(p.r) * k, float(p.g) * k, float(p.b) * k,
                                                              float(p.a) * k);
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
        for (int y = part.top(); y <= part.bottom(); ++y) {
            for (int x = part.left(); x <= part.right(); ++x) {
                const float k = selection.coverage(x, y);
                if (k <= 0.0f)
                    continue;
                Pixel &p = d[(y - tr.top()) * N + (x - tr.left())];
                // A partly selected pixel loses that part of itself.
                const float keep = 1.0f - k;
                p = k >= 1.0f ? clear : makePixel(float(p.r) * keep, float(p.g) * keep, float(p.b) * keep,
                                                  float(p.a) * keep);
            }
        }
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

namespace {

// The canvas area as premultiplied 8-bit sRGB (what you see).
QImage displayImage(const TileStore &store, const QRect &area)
{
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(pixelToDisplay(store.defaultPixel()));
    for (const TileCoord c : TileStore::tilesIntersecting(area)) {
        const QImage tile = store.tile(c);
        if (tile.isNull())
            continue;
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & area;
        const Pixel *src = tilePixels(tile);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            auto *d = reinterpret_cast<QRgb *>(out.scanLine(y - area.top()));
            const Pixel *s = src + (y - tr.top()) * N;
            for (int x = part.left(); x <= part.right(); ++x)
                d[x - area.left()] = pixelToDisplay(s[x - tr.left()]);
        }
    }
    return out;
}

// Transfer curves through interpolated tables: Color to Alpha converts every
// pixel both ways, and pow() made that the slow part.
class Curve
{
public:
    explicit Curve(float (*f)(float))
    {
        for (int i = 0; i <= kSteps; ++i)
            m_table[i] = f(float(i) / kSteps);
    }
    float operator()(float v) const
    {
        const float x = std::clamp(v, 0.0f, 1.0f) * kSteps;
        const int i = std::min(int(x), kSteps - 1);
        const float t = x - float(i);
        return m_table[i] + (m_table[i + 1] - m_table[i]) * t;
    }

private:
    static constexpr int kSteps = 4096;
    float m_table[kSteps + 1];
};

const Curve &toSrgb()
{
    static const Curve c(linearToSrgb);
    return c;
}

const Curve &toLinear()
{
    static const Curve c(srgbToLinear);
    return c;
}

} // namespace

Selection magicWand(const TileStore &store, const QRect &canvas, const QPoint &seed, double tolerance,
                    bool contiguous)
{
    if (!canvas.contains(seed))
        return {};
    const QImage img = displayImage(store, canvas);
    const int w = img.width(), h = img.height();
    const int tol = int(std::lround(std::clamp(tolerance, 0.0, 1.0) * 255.0));
    const QRgb ref = reinterpret_cast<const QRgb *>(img.constScanLine(seed.y() - canvas.top()))[seed.x() - canvas.left()];
    const auto near = [&](QRgb p) {
        return std::abs(qRed(p) - qRed(ref)) <= tol && std::abs(qGreen(p) - qGreen(ref)) <= tol
               && std::abs(qBlue(p) - qBlue(ref)) <= tol && std::abs(qAlpha(p) - qAlpha(ref)) <= tol;
    };
    const auto px = [&](int x, int y) { return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x]; };

    QImage mask(w, h, QImage::Format_Grayscale8);
    mask.fill(0);
    if (!contiguous) {
        for (int y = 0; y < h; ++y) {
            uchar *m = mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                m[x] = near(px(x, y)) ? 255 : 0;
        }
        return Selection::mask(canvas, mask);
    }

    // Scanline flood fill.
    std::vector<QPoint> stack{seed - canvas.topLeft()};
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        uchar *m = mask.scanLine(p.y());
        if (m[p.x()] || !near(px(p.x(), p.y())))
            continue;
        int x0 = p.x(), x1 = p.x();
        while (x0 > 0 && !m[x0 - 1] && near(px(x0 - 1, p.y())))
            --x0;
        while (x1 < w - 1 && !m[x1 + 1] && near(px(x1 + 1, p.y())))
            ++x1;
        std::fill(m + x0, m + x1 + 1, uchar(255));
        for (const int ny : {p.y() - 1, p.y() + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            const uchar *nm = mask.constScanLine(ny);
            bool inRun = false;
            for (int x = x0; x <= x1; ++x) {
                const bool open = !nm[x] && near(px(x, ny));
                if (open && !inRun)
                    stack.push_back(QPoint(x, ny));
                inRun = open;
            }
        }
    }
    return Selection::mask(canvas, mask);
}

QHash<TileCoord, QImage> colorToAlpha(TileStore &store, const Selection &selection, const QRect &canvas,
                                      const QColor &color, double threshold)
{
    QHash<TileCoord, QImage> before;
    const QRect area = selection.isEmpty() ? canvas : (selection.bounds() & canvas);
    if (area.isEmpty())
        return before;
    const QColor c = color.toRgb();
    const float ref[3] = {float(c.redF()), float(c.greenF()), float(c.blueF())};
    const float t = float(std::clamp(threshold, 0.0, 0.999));
    const Curve &enc = toSrgb();
    const Curve &dec = toLinear();

    for (const TileCoord tc : TileStore::tilesIntersecting(area)) {
        const QImage old = store.tile(tc);
        QImage tile = old.isNull() ? filledTile(store.defaultPixel()) : old.copy();
        Pixel *d = tilePixels(tile);
        const QRect tr = TileStore::tileRect(tc);
        const QRect part = tr & area;
        bool changed = false;
        for (int y = part.top(); y <= part.bottom(); ++y) {
            for (int x = part.left(); x <= part.right(); ++x) {
                const float cover = selection.isEmpty() ? 1.0f : selection.coverage(x, y);
                if (cover <= 0.0f)
                    continue;
                Pixel &p = d[(y - tr.top()) * N + (x - tr.left())];
                const float a0 = float(p.a);
                if (a0 <= 0.0f)
                    continue;
                const Pixel was = p;
                // Straight sRGB, 0..1.
                const float s[3] = {enc(float(p.r) / a0), enc(float(p.g) / a0), enc(float(p.b) / a0)};
                float alpha = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    float ai = 0.0f;
                    if (s[i] > ref[i] + 1e-6f)
                        ai = (s[i] - ref[i]) / (1.0f - ref[i]);
                    else if (s[i] < ref[i] - 1e-6f)
                        ai = (ref[i] - s[i]) / ref[i];
                    alpha = std::max(alpha, std::clamp(ai, 0.0f, 1.0f));
                }
                float out[3] = {ref[0], ref[1], ref[2]};
                if (alpha > 0.0f)
                    for (int i = 0; i < 3; ++i)
                        out[i] = std::clamp((s[i] - ref[i]) / alpha + ref[i], 0.0f, 1.0f);
                // Below the threshold: gone; above it, stretched back to full.
                const float kept = alpha <= t ? 0.0f : (alpha - t) / (1.0f - t);
                const float a = kept * a0;
                p = makePixel(dec(out[0]) * a, dec(out[1]) * a, dec(out[2]) * a, a);
                if (cover < 1.0f) {
                    // Partly selected: that much of the way to the result.
                    const float keep = 1.0f - cover;
                    p = makePixel(float(p.r) * cover + float(was.r) * keep, float(p.g) * cover + float(was.g) * keep,
                                  float(p.b) * cover + float(was.b) * keep, float(p.a) * cover + float(was.a) * keep);
                }
                changed = true;
            }
        }
        if (!changed)
            continue;
        before.insert(tc, old);
        store.setTile(tc, tile);
    }
    return before;
}

QRect opaqueBounds(const TileStore &store, const QRect &canvas)
{
    // Half an 8-bit step: anything that would export as alpha 0 doesn't count.
    constexpr float kMin = 0.5f / 255.0f;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    const bool defaultOpaque = float(store.defaultPixel().a) >= kMin;
    for (const TileCoord c : TileStore::tilesIntersecting(canvas)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & canvas;
        const QImage tile = store.tile(c);
        if (tile.isNull()) {
            if (defaultOpaque) {
                x0 = std::min(x0, part.left());
                y0 = std::min(y0, part.top());
                x1 = std::max(x1, part.right());
                y1 = std::max(y1, part.bottom());
            }
            continue;
        }
        const Pixel *src = tilePixels(tile);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const Pixel *s = src + (y - tr.top()) * N;
            for (int x = part.left(); x <= part.right(); ++x) {
                if (float(s[x - tr.left()].a) < kMin)
                    continue;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    }
    return x1 < x0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
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

void FloatingContent::replace(const QImage &content, const Selection &shape, const QPoint &position)
{
    if (!isActive() || content.isNull() || content.format() != TileStore::TileFormat)
        return;
    m_content = content;
    m_shape = shape.isEmpty() ? Selection::rect(QRect(QPoint(0, 0), content.size())) : shape;
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

} // namespace easeletch
