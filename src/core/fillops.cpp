#include "fillops.h"

#include "color.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace easeletch {

namespace {

constexpr int N = TileStore::TileSize;

// src over dst, with src taken at a coverage of k. Premultiplied.
inline void layOver(Pixel &dst, const float src[4], float k)
{
    const float sa = src[3] * k;
    if (sa <= 0.0f)
        return;
    const float keep = 1.0f - sa;
    dst = makePixel(src[0] * k + float(dst.r) * keep, src[1] * k + float(dst.g) * keep,
                    src[2] * k + float(dst.b) * keep, sa + float(dst.a) * keep);
}

// Runs paint(x, y, pixel) over every pixel of area, tile by tile; paint
// returns whether it changed the pixel. Only tiles that changed are replaced
// and reported.
template <typename Paint>
QHash<TileCoord, QImage> paintArea(TileStore &store, const QRect &area, Paint paint)
{
    QHash<TileCoord, QImage> before;
    if (area.isEmpty())
        return before;
    for (const TileCoord tc : TileStore::tilesIntersecting(area)) {
        const QImage old = store.tile(tc);
        QImage tile;
        if (old.isNull()) {
            tile = QImage(N, N, TileStore::TileFormat);
            auto *p = reinterpret_cast<Pixel *>(tile.bits());
            std::fill(p, p + N * N, store.defaultPixel());
        } else {
            tile = old.copy();
        }
        auto *d = reinterpret_cast<Pixel *>(tile.bits());
        const QRect tr = TileStore::tileRect(tc);
        const QRect part = tr & area;
        bool changed = false;
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                changed |= paint(x, y, d[(y - tr.top()) * N + (x - tr.left())]);
        if (!changed)
            continue;
        before.insert(tc, old);
        store.setTile(tc, tile);
    }
    return before;
}

} // namespace

QHash<TileCoord, QImage> fillRegion(TileStore &store, const Selection &region, const Selection &clip,
                                    const QRect &canvas, const QColor &color)
{
    if (region.isEmpty())
        return {};
    QRect area = region.bounds() & canvas;
    if (!clip.isEmpty())
        area &= clip.bounds();
    const Pixel p = pixelFromColor(color);
    const float src[4] = {float(p.r), float(p.g), float(p.b), float(p.a)};
    return paintArea(store, area, [&](int x, int y, Pixel &dst) {
        float k = region.coverage(x, y);
        if (k > 0.0f && !clip.isEmpty())
            k *= clip.coverage(x, y);
        if (k <= 0.0f)
            return false;
        const Pixel was = dst;
        layOver(dst, src, k);
        return !samePixel(was, dst);
    });
}

Pixel gradientPixel(const Gradient &gradient, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    QColor a = gradient.start.toRgb(), b = gradient.end.toRgb();
    // A transparent end has no colour of its own: it's the other end, faded out.
    if (a.alpha() == 0 && b.alpha() != 0)
        a = QColor(b.red(), b.green(), b.blue(), 0);
    else if (b.alpha() == 0 && a.alpha() != 0)
        b = QColor(a.red(), a.green(), a.blue(), 0);
    const auto mix = [t](double x, double y) { return float(x + (y - x) * t); };
    const float alpha = mix(a.alphaF(), b.alphaF());
    return makePixel(srgbToLinear(mix(a.redF(), b.redF())) * alpha, srgbToLinear(mix(a.greenF(), b.greenF())) * alpha,
                     srgbToLinear(mix(a.blueF(), b.blueF())) * alpha, alpha);
}

QHash<TileCoord, QImage> fillGradient(TileStore &store, const Selection &clip, const QRect &canvas,
                                      const Gradient &gradient)
{
    const QRect area = clip.isEmpty() ? canvas : (clip.bounds() & canvas);
    const QPointF axis = gradient.to - gradient.from;
    const double length2 = axis.x() * axis.x() + axis.y() * axis.y();
    if (area.isEmpty() || length2 <= 0.0)
        return {};
    const double length = std::sqrt(length2);

    // The ramp, worked out once: finer than any pixel step of a 16-bit ramp
    // across a screen, and mixed linearly between entries.
    constexpr int kSteps = 4096;
    std::vector<float> ramp(size_t(kSteps + 1) * 4);
    for (int i = 0; i <= kSteps; ++i) {
        const Pixel p = gradientPixel(gradient, double(i) / kSteps);
        float *r = &ramp[size_t(i) * 4];
        r[0] = float(p.r);
        r[1] = float(p.g);
        r[2] = float(p.b);
        r[3] = float(p.a);
    }

    return paintArea(store, area, [&](int x, int y, Pixel &dst) {
        const float k = clip.isEmpty() ? 1.0f : clip.coverage(x, y);
        if (k <= 0.0f)
            return false;
        const double px = x + 0.5 - gradient.from.x(), py = y + 0.5 - gradient.from.y();
        const double t = gradient.radial ? std::sqrt(px * px + py * py) / length
                                         : (px * axis.x() + py * axis.y()) / length2;
        const double pos = std::clamp(t, 0.0, 1.0) * kSteps;
        const int i = std::min(int(pos), kSteps - 1);
        const float f = float(pos - i);
        const float *lo = &ramp[size_t(i) * 4], *hi = lo + 4;
        const float src[4] = {lo[0] + (hi[0] - lo[0]) * f, lo[1] + (hi[1] - lo[1]) * f, lo[2] + (hi[2] - lo[2]) * f,
                              lo[3] + (hi[3] - lo[3]) * f};
        const Pixel was = dst;
        layOver(dst, src, k);
        return !samePixel(was, dst);
    });
}

} // namespace easeletch
