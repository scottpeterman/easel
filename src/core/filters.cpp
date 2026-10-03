#include "filters.h"

#include "color.h"
#include "regionops.h"
#include "srgblut.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace easeletch {

namespace {

constexpr int N = TileStore::TileSize;

// A rectangle of the layer as floats: RGBA, premultiplied, linear light.
struct Buffer {
    QRect rect;
    std::vector<float> px;

    int w() const { return rect.width(); }
    int h() const { return rect.height(); }
    float *at(int x, int y) { return &px[(size_t(y) * size_t(w()) + size_t(x)) * 4]; }
    const float *at(int x, int y) const { return &px[(size_t(y) * size_t(w()) + size_t(x)) * 4]; }
};

Buffer readBuffer(const TileStore &store, const QRect &rect)
{
    Buffer b;
    b.rect = rect;
    b.px.resize(size_t(rect.width()) * size_t(rect.height()) * 4);
    const Pixel d = store.defaultPixel();
    const float def[4] = {float(d.r), float(d.g), float(d.b), float(d.a)};
    for (const TileCoord c : TileStore::tilesIntersecting(rect)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & rect;
        const QImage tile = store.tile(c);
        const auto *src = tile.isNull() ? nullptr : reinterpret_cast<const Pixel *>(tile.constBits());
        for (int y = part.top(); y <= part.bottom(); ++y) {
            float *out = b.at(part.left() - rect.left(), y - rect.top());
            for (int x = part.left(); x <= part.right(); ++x, out += 4) {
                if (!src) {
                    std::copy(def, def + 4, out);
                    continue;
                }
                const Pixel &p = src[(y - tr.top()) * N + (x - tr.left())];
                out[0] = float(p.r);
                out[1] = float(p.g);
                out[2] = float(p.b);
                out[3] = float(p.a);
            }
        }
    }
    return b;
}

// A box blur along every row: radius r each side, the ends repeated outward.
void boxRows(const float *src, float *dst, int w, int h, int r)
{
    const double norm = 1.0 / double(2 * r + 1);
    for (int y = 0; y < h; ++y) {
        const float *s = src + size_t(y) * size_t(w) * 4;
        float *d = dst + size_t(y) * size_t(w) * 4;
        const auto px = [&](int x) { return s + size_t(std::clamp(x, 0, w - 1)) * 4; };
        double sum[4] = {0, 0, 0, 0};
        for (int x = -r; x <= r; ++x)
            for (int ch = 0; ch < 4; ++ch)
                sum[ch] += double(px(x)[ch]);
        for (int x = 0; x < w; ++x) {
            const float *add = px(x + r + 1), *drop = px(x - r);
            for (int ch = 0; ch < 4; ++ch) {
                d[size_t(x) * 4 + size_t(ch)] = float(sum[ch] * norm);
                sum[ch] += double(add[ch]) - double(drop[ch]);
            }
        }
    }
}

// The same down every column, a row at a time so memory is read in order.
void boxColumns(const float *src, float *dst, int w, int h, int r)
{
    const double norm = 1.0 / double(2 * r + 1);
    const size_t n = size_t(w) * 4;
    const auto row = [&](int y) { return src + size_t(std::clamp(y, 0, h - 1)) * n; };
    std::vector<double> sum(n, 0.0);
    for (int y = -r; y <= r; ++y) {
        const float *s = row(y);
        for (size_t i = 0; i < n; ++i)
            sum[i] += double(s[i]);
    }
    for (int y = 0; y < h; ++y) {
        float *d = dst + size_t(y) * n;
        const float *add = row(y + r + 1), *drop = row(y - r);
        for (size_t i = 0; i < n; ++i) {
            d[i] = float(sum[i] * norm);
            sum[i] += double(add[i]) - double(drop[i]);
        }
    }
}

// A true Gaussian along one direction, for small radii where boxes are too coarse.
void kernelPass(const float *src, float *dst, int count, int lines, int step, int lineStep,
                const std::vector<float> &kernel)
{
    const int r = int(kernel.size()) / 2;
    for (int line = 0; line < lines; ++line) {
        const float *s = src + size_t(line) * size_t(lineStep);
        float *d = dst + size_t(line) * size_t(lineStep);
        for (int i = 0; i < count; ++i) {
            float acc[4] = {0, 0, 0, 0};
            for (int k = -r; k <= r; ++k) {
                const float *p = s + size_t(std::clamp(i + k, 0, count - 1)) * size_t(step);
                const float wgt = kernel[size_t(k + r)];
                for (int ch = 0; ch < 4; ++ch)
                    acc[ch] += p[ch] * wgt;
            }
            std::copy(acc, acc + 4, d + size_t(i) * size_t(step));
        }
    }
}

// Gaussian blur of the whole buffer, sigma in pixels.
void gaussianBlur(Buffer &b, double sigma)
{
    if (sigma <= 0.0 || b.px.empty())
        return;
    const int w = b.w(), h = b.h();
    std::vector<float> tmp(b.px.size());
    const int rowStep = 4, rowLine = w * 4; // along a row: next pixel, next row
    const int colStep = w * 4, colLine = 4; // along a column: next row, next column

    if (sigma < 2.0) {
        const int r = std::max(1, int(std::ceil(sigma * 3.0)));
        std::vector<float> kernel(size_t(2 * r + 1));
        float total = 0.0f;
        for (int k = -r; k <= r; ++k)
            total += kernel[size_t(k + r)] = float(std::exp(-(k * k) / (2.0 * sigma * sigma)));
        for (float &k : kernel)
            k /= total;
        kernelPass(b.px.data(), tmp.data(), w, h, rowStep, rowLine, kernel);
        kernelPass(tmp.data(), b.px.data(), h, w, colStep, colLine, kernel);
        return;
    }
    // Three box blurs come within a few percent of a Gaussian, at a cost that
    // doesn't grow with the radius.
    const double ideal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
    int lower = int(std::floor(ideal));
    if (lower % 2 == 0)
        --lower;
    const int upper = lower + 2;
    const int lowerCount = int(std::lround((12.0 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9.0)
                                           / (-4.0 * lower - 4.0)));
    for (int pass = 0; pass < 3; ++pass) {
        const int r = ((pass < lowerCount ? lower : upper) - 1) / 2;
        boxRows(b.px.data(), tmp.data(), w, h, r);
        boxColumns(tmp.data(), b.px.data(), w, h, r);
    }
}

// A number in -1..1 fixed by the pixel, the channel and the seed.
float noiseAt(int x, int y, int channel, quint32 seed)
{
    quint32 hsh = quint32(x) * 0x9E3779B1u ^ quint32(y) * 0x85EBCA77u ^ quint32(channel) * 0xC2B2AE3Du ^ seed * 0x27D4EB2Fu;
    hsh ^= hsh >> 15;
    hsh *= 0x2C1B3C6Du;
    hsh ^= hsh >> 12;
    hsh *= 0x297A2D39u;
    hsh ^= hsh >> 15;
    return float(hsh) / 2147483648.0f - 1.0f;
}

int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// How far a filter reads beyond the pixels it changes.
int marginFor(const Filter &f)
{
    switch (f.type) {
    case FilterType::GaussianBlur:
    case FilterType::Sharpen:
        return int(std::ceil(f.radius * 3.0)) + 1;
    case FilterType::Pixelate:
        return f.cell;
    case FilterType::Noise:
        break;
    }
    return 0;
}

} // namespace

Filter Filter::make(FilterType type)
{
    Filter f;
    f.type = type;
    switch (type) {
    case FilterType::GaussianBlur:
        f.radius = 4.0;
        break;
    case FilterType::Sharpen:
        f.radius = 1.5;
        f.amount = 1.0;
        break;
    case FilterType::Noise:
        f.amount = 0.15;
        break;
    case FilterType::Pixelate:
        f.cell = 8;
        break;
    }
    return f;
}

Filter Filter::normalized() const
{
    Filter f = *this;
    const bool sharpen = f.type == FilterType::Sharpen;
    f.radius = std::isfinite(f.radius) ? std::clamp(f.radius, 0.1, sharpen ? 50.0 : 250.0) : 1.0;
    f.amount = std::isfinite(f.amount) ? std::clamp(f.amount, 0.0, sharpen ? 5.0 : 1.0) : 0.0;
    f.cell = std::clamp(f.cell, 2, 256);
    return f;
}

QHash<TileCoord, QImage> applyFilter(TileStore &store, const Selection &clip, const QRect &canvas, const Filter &filter)
{
    const Filter f = filter.normalized();
    QRect area = clip.isEmpty() ? canvas : (clip.bounds() & canvas);
    const int margin = marginFor(f);
    // Where the layer is empty and stays empty, there's nothing to do: keep
    // to what's painted, plus as far as the filter can spread it.
    if (float(store.defaultPixel().a) <= 0.0f) {
        const QRect painted = opaqueBounds(store, canvas);
        if (painted.isEmpty())
            return {};
        area &= painted.adjusted(-margin, -margin, margin, margin);
    }
    if (area.isEmpty())
        return {};

    const QRect read = area.adjusted(-margin, -margin, margin, margin) & canvas;
    const Buffer in = readBuffer(store, read);
    Buffer out = in;

    switch (f.type) {
    case FilterType::GaussianBlur:
        gaussianBlur(out, f.radius);
        break;
    case FilterType::Sharpen: {
        // Unsharp mask: push each pixel away from its blurred self.
        gaussianBlur(out, f.radius);
        const float k = float(f.amount);
        for (size_t i = 0; i < out.px.size(); i += 4) {
            const float a = std::clamp(in.px[i + 3] + (in.px[i + 3] - out.px[i + 3]) * k, 0.0f, 1.0f);
            for (size_t ch = 0; ch < 3; ++ch)
                out.px[i + ch] = std::clamp(in.px[i + ch] + (in.px[i + ch] - out.px[i + ch]) * k, 0.0f, a);
            out.px[i + 3] = a;
        }
        break;
    }
    case FilterType::Noise: {
        // Added to the sRGB values, so it looks as strong in the shadows as
        // in the highlights.
        const srgblut::Luts &l = srgblut::luts();
        const float k = float(f.amount) * 0.5f;
        for (int y = 0; y < out.h(); ++y) {
            for (int x = 0; x < out.w(); ++x) {
                float *p = out.at(x, y);
                const float a = p[3];
                if (a <= 0.0f)
                    continue;
                const int cx = read.left() + x, cy = read.top() + y;
                const float mono = noiseAt(cx, cy, 0, f.seed);
                for (int ch = 0; ch < 3; ++ch) {
                    const float n = f.monochrome ? mono : noiseAt(cx, cy, ch, f.seed);
                    const float v = std::clamp(srgblut::encode(l, p[ch] / a) + n * k, 0.0f, 1.0f);
                    p[ch] = srgblut::decode(l, v) * a;
                }
            }
        }
        break;
    }
    case FilterType::Pixelate: {
        const int s = f.cell;
        const int cx0 = floorDiv(read.left(), s), cx1 = floorDiv(read.right(), s);
        const int cy0 = floorDiv(read.top(), s), cy1 = floorDiv(read.bottom(), s);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                // Each block takes the average of what was in it. A block cut
                // by the canvas edge averages the part that's on the canvas;
                // one cut by the edge of what was read is left as it was (it
                // lies outside the area being changed).
                const QRect whole = QRect(cx * s, cy * s, s, s) & canvas;
                const QRect cell = whole & read;
                if (cell.isEmpty() || cell != whole)
                    continue;
                double sum[4] = {0, 0, 0, 0};
                for (int y = cell.top(); y <= cell.bottom(); ++y) {
                    const float *p = in.at(cell.left() - read.left(), y - read.top());
                    for (int x = 0; x < cell.width(); ++x, p += 4)
                        for (int ch = 0; ch < 4; ++ch)
                            sum[ch] += double(p[ch]);
                }
                const double n = double(cell.width()) * cell.height();
                const float avg[4] = {float(sum[0] / n), float(sum[1] / n), float(sum[2] / n), float(sum[3] / n)};
                for (int y = cell.top(); y <= cell.bottom(); ++y) {
                    float *p = out.at(cell.left() - read.left(), y - read.top());
                    for (int x = 0; x < cell.width(); ++x, p += 4)
                        std::copy(avg, avg + 4, p);
                }
            }
        }
        break;
    }
    }

    // Back into the tiles: only where it's selected, only tiles that changed.
    QHash<TileCoord, QImage> before;
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
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const float *o = out.at(part.left() - read.left(), y - read.top());
            const float *i = in.at(part.left() - read.left(), y - read.top());
            for (int x = part.left(); x <= part.right(); ++x, o += 4, i += 4) {
                const float k = clip.isEmpty() ? 1.0f : clip.coverage(x, y);
                if (k <= 0.0f)
                    continue;
                Pixel &dst = d[(y - tr.top()) * N + (x - tr.left())];
                const Pixel was = dst;
                dst = k >= 1.0f ? makePixel(o[0], o[1], o[2], o[3])
                                : makePixel(i[0] + (o[0] - i[0]) * k, i[1] + (o[1] - i[1]) * k,
                                            i[2] + (o[2] - i[2]) * k, i[3] + (o[3] - i[3]) * k);
                changed |= !samePixel(was, dst);
            }
        }
        if (!changed)
            continue;
        before.insert(tc, old);
        store.setTile(tc, tile);
    }
    return before;
}

} // namespace easeletch
