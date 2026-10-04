#include "heal.h"

#include "color.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace easeletch {

namespace {

constexpr int N = TileStore::TileSize;
constexpr int kMaxRingSamples = 2000;

// A rectangle of the layer as floats: RGBA, premultiplied, linear light.
struct Buffer {
    QRect rect;
    std::vector<float> px;

    int w() const { return rect.width(); }
    size_t index(int x, int y) const { return (size_t(y - rect.top()) * size_t(w()) + size_t(x - rect.left())) * 4; }
    const float *at(int x, int y) const { return &px[index(x, y)]; }
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
            float *out = &b.px[b.index(part.left(), y)];
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

} // namespace

bool healArea(TileStore &store, const QRect &canvas, const QRect &areaIn, const std::vector<float> &coverage)
{
    if (areaIn.isEmpty() || coverage.size() != size_t(areaIn.width()) * size_t(areaIn.height()))
        return false;
    const auto cov = [&](int x, int y) {
        return coverage[size_t(y - areaIn.top()) * size_t(areaIn.width()) + size_t(x - areaIn.left())];
    };
    // The marks themselves: the part of the area that's on the canvas and covered.
    QRect area;
    for (int y = areaIn.top(); y <= areaIn.bottom(); ++y)
        for (int x = areaIn.left(); x <= areaIn.right(); ++x)
            if (cov(x, y) > 0.0f && canvas.contains(x, y))
                area |= QRect(x, y, 1, 1);
    if (area.isEmpty())
        return false;
    const int aw = area.width(), ah = area.height();
    // How far round the marks the surroundings are compared: wider for bigger
    // marks, so a line running through them is matched by its direction too.
    const int kRing = std::clamp(std::min(aw, ah) / 8, 3, 8);
    const auto marked = [&](int x, int y) { return area.contains(x, y) && cov(x, y) > 0.0f; };

    // How far to look for something to copy.
    const int reach = std::clamp(2 * std::max(aw, ah), 40, 400);
    const QRect window = area.adjusted(-reach, -reach, reach, reach) & canvas;
    const Buffer in = readBuffer(store, window);

    // The surroundings: unmarked pixels within a few pixels of a marked one.
    struct Sample {
        int x, y;
    };
    std::vector<Sample> ring;
    const QRect ringBox = area.adjusted(-kRing, -kRing, kRing, kRing) & window;
    for (int y = ringBox.top(); y <= ringBox.bottom(); ++y) {
        for (int x = ringBox.left(); x <= ringBox.right(); ++x) {
            if (marked(x, y))
                continue;
            bool near = false;
            for (int dy = -kRing; dy <= kRing && !near; ++dy)
                for (int dx = -kRing; dx <= kRing && !near; ++dx)
                    near = marked(x + dx, y + dy);
            if (near)
                ring.push_back({x, y});
        }
    }
    if (ring.empty())
        return false; // everything is marked: nothing to go by

    // Where to copy from: the offset whose surroundings look most like these.
    // The patch copied must lie clear of the marks, and on the canvas.
    std::vector<Sample> probe;
    const size_t stride = std::max<size_t>(1, ring.size() / kMaxRingSamples);
    for (size_t i = 0; i < ring.size(); i += stride)
        probe.push_back(ring[i]);
    const int step = std::clamp(std::min(aw, ah) / 6, 1, 8);
    const double none = std::numeric_limits<double>::max();
    // How unlike the surroundings are at an offset (lower is better), or
    // `none` where a patch can't be taken or it's already worse than `limit`.
    const auto score = [&](int dx, int dy, double limit) {
        if (std::abs(dx) < aw + 2 * kRing && std::abs(dy) < ah + 2 * kRing)
            return none; // would copy the marks themselves
        if (!window.contains(ringBox.translated(dx, dy)))
            return none;
        // A little in favour of nearer patches, which are likelier to be the
        // same material in the same light.
        const double weight = 1.0 + 0.25 * std::hypot(double(dx), double(dy)) / reach;
        double sum = 0.0;
        for (const Sample &s : probe) {
            const float *a = in.at(s.x, s.y), *b = in.at(s.x + dx, s.y + dy);
            for (int ch = 0; ch < 4; ++ch) {
                const double d = double(a[ch]) - double(b[ch]);
                sum += d * d;
            }
            if (sum * weight >= limit)
                return none;
        }
        return sum * weight + 1e-9 * (std::abs(dx) + std::abs(dy));
    };
    // A coarse sweep, keeping the few best...
    struct Candidate {
        double score;
        int dx, dy;
    };
    constexpr int kKeep = 4;
    std::vector<Candidate> top;
    for (int dy = -reach; dy <= reach; dy += step) {
        for (int dx = -reach; dx <= reach; dx += step) {
            const double limit = int(top.size()) < kKeep ? none : top.back().score;
            const double sc = score(dx, dy, limit);
            if (sc == none)
                continue;
            top.push_back({sc, dx, dy});
            std::sort(top.begin(), top.end(), [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
            if (int(top.size()) > kKeep)
                top.pop_back();
        }
    }
    // ...then each walked to the exact pixel: a line running through the
    // marks only joins up if the patch lines up with it.
    bool found = false;
    int bestDx = 0, bestDy = 0;
    double best = none;
    for (Candidate c : top) {
        for (int r = std::max(1, step / 2); r >= 1; r = r > 1 ? (r + 1) / 2 : 0) {
            bool moved = true;
            while (moved) {
                moved = false;
                for (int oy = -r; oy <= r; oy += r) {
                    for (int ox = -r; ox <= r; ox += r) {
                        if (ox == 0 && oy == 0)
                            continue;
                        const double sc = score(c.dx + ox, c.dy + oy, c.score);
                        if (sc < c.score) {
                            c = {sc, c.dx + ox, c.dy + oy};
                            moved = true;
                        }
                    }
                }
            }
        }
        if (c.score < best) {
            best = c.score;
            bestDx = c.dx;
            bestDy = c.dy;
            found = true;
        }
    }

    // The correction: the difference between what's here and what's copied,
    // known on the surroundings and spread smoothly across the marks (each
    // marked pixel becomes the average of its neighbours). With nothing to
    // copy the "difference" is the picture itself, which gives a smooth fill.
    const auto source = [&](int x, int y, int ch) {
        return found ? in.at(x + bestDx, y + bestDy)[ch] : 0.0f;
    };
    // The difference at a pixel of the surroundings. It's there to even out
    // light and tone, so it's kept modest: where the copy simply doesn't
    // match (a line the patch hasn't got), a big difference spread inward
    // would smear that line across the repair.
    constexpr float kMaxShift = 0.12f;
    const auto shift = [&](int x, int y, int ch) {
        const float d = in.at(x, y)[ch] - source(x, y, ch);
        return found ? std::clamp(d, -kMaxShift, kMaxShift) : d;
    };
    const size_t cells = size_t(aw) * size_t(ah);
    std::vector<float> u(cells * 4, 0.0f);
    const auto cell = [&](int x, int y) { return (size_t(y - area.top()) * size_t(aw) + size_t(x - area.left())) * 4; };
    double mean[4] = {0, 0, 0, 0};
    for (const Sample &s : ring)
        for (int ch = 0; ch < 4; ++ch)
            mean[ch] += double(shift(s.x, s.y, ch));
    for (double &m : mean)
        m /= double(ring.size());
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (marked(x, y))
                for (int ch = 0; ch < 4; ++ch)
                    u[cell(x, y) + size_t(ch)] = float(mean[ch]);

    const int span = std::max(2, std::min(aw, ah));
    const float omega = float(2.0 / (1.0 + std::sin(3.14159265358979 / (span + 1))));
    const int rounds = std::clamp(4 * span, 30, 800);
    const int ox[4] = {1, -1, 0, 0}, oy[4] = {0, 0, 1, -1};
    for (int round = 0; round < rounds; ++round) {
        float moved = 0.0f;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                if (!marked(x, y))
                    continue;
                float sum[4] = {0, 0, 0, 0};
                int n = 0;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + ox[k], ny = y + oy[k];
                    if (!window.contains(nx, ny))
                        continue; // the edge of the canvas: nothing known beyond it
                    ++n;
                    if (marked(nx, ny)) {
                        const float *v = &u[cell(nx, ny)];
                        for (int ch = 0; ch < 4; ++ch)
                            sum[ch] += v[ch];
                    } else {
                        for (int ch = 0; ch < 4; ++ch)
                            sum[ch] += shift(nx, ny, ch);
                    }
                }
                if (n == 0)
                    continue;
                float *v = &u[cell(x, y)];
                for (int ch = 0; ch < 4; ++ch) {
                    const float next = v[ch] + omega * (sum[ch] / float(n) - v[ch]);
                    moved = std::max(moved, std::abs(next - v[ch]));
                    v[ch] = next;
                }
            }
        }
        if (moved < 2e-4f)
            break;
    }

    // Into the tiles, each pixel by its coverage.
    bool changed = false;
    for (const TileCoord tc : TileStore::tilesIntersecting(area)) {
        const QRect tr = TileStore::tileRect(tc);
        const QRect part = tr & area;
        Pixel *d = nullptr;
        for (int y = part.top(); y <= part.bottom(); ++y) {
            for (int x = part.left(); x <= part.right(); ++x) {
                const float k = std::min(cov(x, y), 1.0f);
                if (k <= 0.0f)
                    continue;
                const float *was = in.at(x, y);
                const float *v = &u[cell(x, y)];
                float out[4];
                for (int ch = 0; ch < 4; ++ch)
                    out[ch] = source(x, y, ch) + v[ch];
                out[3] = std::clamp(out[3], 0.0f, 1.0f);
                for (int ch = 0; ch < 3; ++ch)
                    out[ch] = std::clamp(out[ch], 0.0f, out[3]);
                for (int ch = 0; ch < 4; ++ch)
                    out[ch] = was[ch] + (out[ch] - was[ch]) * k;
                if (!d)
                    d = reinterpret_cast<Pixel *>(store.writableTile(tc).bits());
                d[(y - tr.top()) * N + (x - tr.left())] = makePixel(out[0], out[1], out[2], out[3]);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace easeletch
