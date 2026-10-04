#include "shade.h"

#include "color.h"
#include "srgblut.h"

#include <algorithm>
#include <cmath>

namespace easeletch {

namespace {

constexpr int N = TileStore::TileSize;
constexpr int kTolerance = 77;   // of 255: how far an area's pixels may be from where it started
constexpr int kWhite = 225;      // every channel at least this: still white
constexpr int kEmptyAlpha = 26;  // alpha at most this: empty
constexpr double kDark = 0.30;   // darker than this is line work, not an area
constexpr int kLutSize = 256;

QColor mixed(const QColor &a, const QColor &b, double k)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * k), float(a.greenF() + (b.greenF() - a.greenF()) * k),
                            float(a.blueF() + (b.blueF() - a.blueF()) * k), a.alphaF());
}

double lightness(const QColor &c)
{
    return 0.30 * c.redF() + 0.59 * c.greenF() + 0.11 * c.blueF();
}

} // namespace

GradientStops ShadeSettings::softShadeStops()
{
    return {{0.0, QColor(244, 244, 244)}, {1.0, QColor(170, 170, 170)}};
}

ShadeSettings ShadeSettings::normalized() const
{
    ShadeSettings s = *this;
    s.stops = normalizedStops(s.stops);
    s.angle = std::isfinite(s.angle) ? std::fmod(std::fmod(s.angle, 360.0) + 360.0, 360.0) : 0.0;
    s.brightness = std::isfinite(s.brightness) ? std::clamp(s.brightness, -1.0, 1.0) : 0.0;
    s.variation = std::isfinite(s.variation) ? std::clamp(s.variation, 0.0, 1.0) : 0.0;
    return s;
}

GradientStops ShadeSettings::stopsFor(double shift) const
{
    GradientStops out = normalizedStops(stops);
    const double light = std::clamp(brightness + shift * variation, -1.0, 1.0);
    for (GradientStop &stop : out) {
        QColor c = stop.color.toRgb();
        if (tint.isValid()) {
            const QColor t = tint.toRgb();
            const QColor tinted = QColor::fromRgbF(float(c.redF() * t.redF()), float(c.greenF() * t.greenF()),
                                                   float(c.blueF() * t.blueF()), c.alphaF());
            // The top of the range keeps some of its white: a reflection of
            // the light itself, not of the metal's colour.
            const double shine = std::clamp((lightness(c) - 0.8) / 0.2, 0.0, 1.0);
            c = mixed(tinted, c, shine * shine * 0.7);
        }
        c = light >= 0.0 ? mixed(c, QColor(255, 255, 255, c.alpha()), light) : mixed(c, QColor(0, 0, 0, c.alpha()), -light);
        stop.color = c;
    }
    return out;
}

void AreaShader::findAreas(const TileStore &lineArt, const Selection &within, const QRect &canvas, const AreaOptions &options)
{
    m_rect = canvas;
    m_areas.clear();
    m_owner.clear();
    const int w = canvas.width(), h = canvas.height();
    const size_t count = size_t(std::max(w, 0)) * size_t(std::max(h, 0));
    if (count == 0)
        return;
    const srgblut::Luts &l = srgblut::luts();

    // The layer as 8-bit sRGB and alpha, as the magic wand sees it.
    std::vector<quint32> rgba(count);
    {
        const Pixel d = lineArt.defaultPixel();
        const auto pack = [&l](const Pixel &p) {
            const float a = std::clamp(float(p.a), 0.0f, 1.0f);
            quint32 v = quint32(std::lround(a * 255.0f)) << 24;
            if (a > 0.0f) {
                const float c[3] = {float(p.r), float(p.g), float(p.b)};
                for (int ch = 0; ch < 3; ++ch)
                    v |= quint32(std::lround(srgblut::encode(l, std::clamp(c[ch] / a, 0.0f, 1.0f)) * 255.0f)) << (ch * 8);
            }
            return v;
        };
        std::fill(rgba.begin(), rgba.end(), pack(d));
        for (const TileCoord c : TileStore::tilesIntersecting(canvas)) {
            const QImage tile = lineArt.tile(c);
            if (tile.isNull())
                continue;
            const auto *src = reinterpret_cast<const Pixel *>(tile.constBits());
            const QRect tr = TileStore::tileRect(c);
            const QRect part = tr & canvas;
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x)
                    rgba[size_t(y - canvas.top()) * size_t(w) + size_t(x - canvas.left())] =
                        pack(src[(y - tr.top()) * N + (x - tr.left())]);
        }
    }
    const auto channel = [](quint32 v, int ch) { return int((v >> (ch * 8)) & 0xFF); };
    const auto alpha = [](quint32 v) { return int(v >> 24); };
    // Where an area can start: on white or nothing, or (not white only) on
    // anything opaque that isn't dark.
    const auto startsArea = [&](quint32 v) {
        if (alpha(v) <= kEmptyAlpha)
            return true;
        if (alpha(v) < 230)
            return false;
        if (options.whiteOnly)
            return channel(v, 0) >= kWhite && channel(v, 1) >= kWhite && channel(v, 2) >= kWhite;
        return 0.30 * channel(v, 0) + 0.59 * channel(v, 1) + 0.11 * channel(v, 2) >= kDark * 255.0;
    };
    const auto alike = [&](quint32 p, quint32 q) {
        for (int s = 0; s < 32; s += 8)
            if (std::abs(int((p >> s) & 0xFF) - int((q >> s) & 0xFF)) > kTolerance)
                return false;
        return true;
    };

    // 1. Regions: flooded outward from each starting pixel, taking in what's
    // close to that pixel's colour (the soft inner edge of the lines with it).
    enum : int { None = -1 };
    std::vector<int> region(count, None);
    struct Region {
        qint64 area = 0;
        qint64 selected = 0;
        bool onEdge = false;
        int areaIndex = -1; // among the areas kept
    };
    std::vector<Region> regions;
    std::vector<size_t> stack;
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    const bool anySelection = !within.isEmpty();
    for (size_t start = 0; start < count; ++start) {
        if (region[start] != None || !startsArea(rgba[start]))
            continue;
        const int id = int(regions.size());
        const quint32 seed = rgba[start];
        Region r;
        region[start] = id;
        stack.assign(1, start);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            ++r.area;
            r.onEdge = r.onEdge || x == 0 || y == 0 || x == w - 1 || y == h - 1;
            if (anySelection && within.coverage(canvas.left() + x, canvas.top() + y) > 0.5f)
                ++r.selected;
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const size_t n = size_t(ny) * size_t(w) + size_t(nx);
                if (region[n] == None && alike(seed, rgba[n])) {
                    region[n] = id;
                    stack.push_back(n);
                }
            }
        }
        regions.push_back(r);
    }

    // 2. The ones to shade.
    const int minArea = std::max(1, options.minArea);
    for (Region &r : regions) {
        const bool chosen = !anySelection || r.selected * 2 > r.area;
        if (r.area >= minArea && !r.onEdge && chosen) {
            r.areaIndex = int(m_areas.size());
            m_areas.push_back({});
        }
    }
    if (m_areas.empty())
        return;
    m_owner.assign(count, -1);
    // Small regions (a fleck of white in the ink) are line work as far as
    // the reach under the lines goes; bigger ones left out keep their edges.
    std::vector<char> open(count, 0);
    for (size_t i = 0; i < count; ++i) {
        const int id = region[i];
        if (id == None || regions[size_t(id)].area < minArea)
            open[i] = 1;
        else if (regions[size_t(id)].areaIndex >= 0)
            m_owner[i] = regions[size_t(id)].areaIndex;
    }

    // 3. A few pixels under the lines round each area, a ring at a time.
    std::vector<size_t> edge, next;
    for (size_t i = 0; i < count; ++i)
        if (m_owner[i] >= 0)
            edge.push_back(i);
    for (int ring = 0; ring < std::clamp(options.grow, 0, 32) && !edge.empty(); ++ring) {
        next.clear();
        for (const size_t i : edge) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const size_t n = size_t(ny) * size_t(w) + size_t(nx);
                if (open[n] && m_owner[n] < 0) {
                    m_owner[n] = m_owner[i];
                    next.push_back(n);
                }
            }
        }
        edge.swap(next);
    }
    for (size_t i = 0; i < count; ++i)
        if (m_owner[i] >= 0)
            m_areas[size_t(m_owner[i])].box |= QRect(canvas.left() + int(i % size_t(w)), canvas.top() + int(i / size_t(w)), 1, 1);
}

int AreaShader::areaAt(int x, int y) const
{
    if (m_owner.empty() || !m_rect.contains(x, y))
        return -1;
    return m_owner[size_t(y - m_rect.top()) * size_t(m_rect.width()) + size_t(x - m_rect.left())];
}

QHash<TileCoord, QImage> AreaShader::shade(TileStore &target, const ShadeSettings &settings) const
{
    QHash<TileCoord, QImage> before;
    if (m_areas.empty())
        return before;
    const ShadeSettings s = settings.normalized();
    const double rad = s.angle * 3.14159265358979 / 180.0;
    const double ux = std::cos(rad), uy = std::sin(rad);

    // Each area: its own run of the gradient, from one side of it to the
    // other along the angle, and its own share of the variation.
    struct Ramp {
        double start = 0.0, scale = 0.0; // position along the angle -> 0..1
        std::vector<Pixel> lut;
    };
    std::vector<Ramp> ramps(m_areas.size());
    QRect all;
    for (size_t i = 0; i < m_areas.size(); ++i) {
        const QRect b = m_areas[i].box;
        all |= b;
        const double xs[2] = {double(b.left()), double(b.right() + 1)}, ys[2] = {double(b.top()), double(b.bottom() + 1)};
        double lo = 1e300, hi = -1e300;
        for (const double x : xs)
            for (const double y : ys) {
                lo = std::min(lo, x * ux + y * uy);
                hi = std::max(hi, x * ux + y * uy);
            }
        // Spread evenly through -0.5..0.5, with neighbours (found one after
        // another) well apart.
        const double shift = double((quint32(i) * 2654435761u) >> 8 & 0xFFFF) / 65535.0 - 0.5;
        const GradientStops stops = s.stopsFor(shift);
        Ramp &r = ramps[i];
        r.start = lo;
        r.scale = hi > lo ? 1.0 / (hi - lo) : 0.0;
        r.lut.resize(kLutSize);
        for (int k = 0; k < kLutSize; ++k)
            r.lut[size_t(k)] = gradientPixel(stops, double(k) / (kLutSize - 1));
    }

    const int w = m_rect.width();
    for (const TileCoord tc : TileStore::tilesIntersecting(all)) {
        const QRect tr = TileStore::tileRect(tc);
        const QRect part = tr & all & m_rect;
        const QImage old = target.tile(tc);
        Pixel *d = nullptr;
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const int *own = &m_owner[size_t(y - m_rect.top()) * size_t(w) + size_t(part.left() - m_rect.left())];
            for (int x = part.left(); x <= part.right(); ++x, ++own) {
                if (*own < 0)
                    continue;
                const Ramp &r = ramps[size_t(*own)];
                const double t = std::clamp(((x + 0.5) * ux + (y + 0.5) * uy - r.start) * r.scale, 0.0, 1.0);
                if (!d) {
                    before.insert(tc, old);
                    d = reinterpret_cast<Pixel *>(target.writableTile(tc).bits());
                }
                d[(y - tr.top()) * N + (x - tr.left())] = r.lut[size_t(std::lround(t * (kLutSize - 1)))];
            }
        }
    }
    return before;
}

} // namespace easeletch
