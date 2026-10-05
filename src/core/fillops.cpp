#include "fillops.h"

#include "color.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
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

QString gradientShapeKey(GradientShape shape)
{
    switch (shape) {
    case GradientShape::Radial: return QStringLiteral("radial");
    case GradientShape::Reflected: return QStringLiteral("reflected");
    case GradientShape::Conical: return QStringLiteral("conical");
    default: return QStringLiteral("linear");
    }
}

GradientShape gradientShapeFromKey(const QString &key)
{
    for (int i = 0; i < GradientShapeCount; ++i)
        if (gradientShapeKey(GradientShape(i)) == key)
            return GradientShape(i);
    return GradientShape::Linear;
}

GradientStops twoStops(const QColor &start, const QColor &end)
{
    return {{0.0, start}, {1.0, end}};
}

GradientStops normalizedStops(GradientStops stops)
{
    stops.removeIf([](const GradientStop &s) { return !s.color.isValid() || std::isnan(s.position); });
    for (GradientStop &s : stops)
        s.position = std::clamp(s.position, 0.0, 1.0);
    std::stable_sort(stops.begin(), stops.end(),
                     [](const GradientStop &a, const GradientStop &b) { return a.position < b.position; });
    if (stops.isEmpty())
        return twoStops(Qt::black, Qt::white);
    if (stops.size() == 1)
        return twoStops(stops.first().color, stops.first().color);
    return stops;
}

GradientStops reversedStops(const GradientStops &stops)
{
    GradientStops out;
    for (auto it = stops.crbegin(); it != stops.crend(); ++it)
        out.append({1.0 - it->position, it->color});
    return out;
}

QString stopsToString(const GradientStops &stops)
{
    QStringList parts;
    for (const GradientStop &s : stops)
        parts << QString::number(s.position, 'g', 6) + QLatin1Char(':') + s.color.name(QColor::HexArgb);
    return parts.join(QLatin1Char(';'));
}

GradientStops stopsFromString(const QString &text)
{
    GradientStops stops;
    for (const QString &part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        bool ok = false;
        const double pos = part.left(colon).toDouble(&ok);
        const QColor color(part.mid(colon + 1));
        if (ok && color.isValid())
            stops.append({pos, color});
    }
    return stops.size() < 2 ? GradientStops() : normalizedStops(stops);
}

Pixel gradientPixel(const GradientStops &stops, double t)
{
    if (stops.isEmpty())
        return makePixel(0, 0, 0, 0);
    t = std::clamp(t, 0.0, 1.0);
    // The pair of stops t lies between (the last pair at a shared position wins,
    // so two stops at one place make a hard edge).
    qsizetype hi = 0;
    while (hi < stops.size() && stops.at(hi).position <= t)
        ++hi;
    const GradientStop &s0 = stops.at(std::max<qsizetype>(hi - 1, 0));
    const GradientStop &s1 = stops.at(std::min(hi, stops.size() - 1));
    const double span = s1.position - s0.position;
    const double f = span > 0.0 ? std::clamp((t - s0.position) / span, 0.0, 1.0) : 0.0;

    QColor a = s0.color.toRgb(), b = s1.color.toRgb();
    // A transparent stop has no colour of its own: it's its neighbour, faded out.
    if (a.alpha() == 0 && b.alpha() != 0)
        a = QColor(b.red(), b.green(), b.blue(), 0);
    else if (b.alpha() == 0 && a.alpha() != 0)
        b = QColor(a.red(), a.green(), a.blue(), 0);
    const auto mix = [f](double x, double y) { return float(x + (y - x) * f); };
    const float alpha = mix(a.alphaF(), b.alphaF());
    return makePixel(srgbToLinear(mix(a.redF(), b.redF())) * alpha, srgbToLinear(mix(a.greenF(), b.greenF())) * alpha,
                     srgbToLinear(mix(a.blueF(), b.blueF())) * alpha, alpha);
}

const QList<GradientPreset> &gradientPresets()
{
    const auto make = [](const char *id, const QString &name, std::initializer_list<std::pair<double, const char *>> stops) {
        GradientPreset p;
        p.id = QString::fromLatin1(id);
        p.name = name;
        for (const auto &[pos, hex] : stops)
            p.stops.append({pos, QColor(QString::fromLatin1(hex))});
        return p;
    };
    // Metals are light and dark bands with sudden turns between them: that's
    // what reads as a reflection.
    static const QList<GradientPreset> presets = {
        make("chrome", QStringLiteral("Chrome"),
             {{0.0, "#f7f9fb"}, {0.18, "#b8c2cc"}, {0.42, "#5d6a78"}, {0.5, "#1f2730"}, {0.52, "#e9eef3"},
              {0.7, "#aab4bf"}, {1.0, "#59646f"}}),
        make("steel", QStringLiteral("Steel"),
             {{0.0, "#dfe3e6"}, {0.25, "#9aa3ab"}, {0.5, "#f2f4f5"}, {0.75, "#7d868e"}, {1.0, "#c9ced2"}}),
        make("gold", QStringLiteral("Gold"),
             {{0.0, "#fff6c8"}, {0.2, "#f2c14e"}, {0.45, "#a86f12"}, {0.55, "#ffe9a0"}, {0.8, "#d99a22"},
              {1.0, "#7a4a08"}}),
        make("copper", QStringLiteral("Copper"),
             {{0.0, "#ffd9c2"}, {0.25, "#d98452"}, {0.5, "#7d3a18"}, {0.6, "#f5b48e"}, {1.0, "#9a4d24"}}),
        make("gunmetal", QStringLiteral("Gunmetal"),
             {{0.0, "#8a939c"}, {0.3, "#3b434b"}, {0.5, "#a9b2ba"}, {0.62, "#4a535b"}, {1.0, "#1c2126"}}),
        // Ends where it starts, so swept round a centre (Conical) it has no seam.
        make("spun-metal", QStringLiteral("Spun metal"),
             {{0.0, "#e8ebee"}, {0.125, "#8f979f"}, {0.25, "#f4f6f7"}, {0.375, "#6f777f"}, {0.5, "#e8ebee"},
              {0.625, "#8f979f"}, {0.75, "#f4f6f7"}, {0.875, "#6f777f"}, {1.0, "#e8ebee"}}),
        // Armour: polished metal in a dark place. Nearly black at both ends,
        // with one narrow band of light that shifts in hue as it brightens
        // (orange through gold to pale yellow) the way a coloured reflection
        // does. An amber for the body, a teal and a magenta for trim.
        make("amber-armour", QStringLiteral("Amber armour"),
             {{0.0, "#1a0d05"}, {0.22, "#7a3208"}, {0.4, "#e07a12"}, {0.5, "#ffd470"}, {0.54, "#fff3c4"},
              {0.6, "#c4620e"}, {0.8, "#4a1c06"}, {1.0, "#120803"}}),
        make("teal-armour", QStringLiteral("Teal armour"),
             {{0.0, "#03110f"}, {0.25, "#0b4f47"}, {0.42, "#18b89c"}, {0.5, "#8ff5dc"}, {0.54, "#e6fff8"},
              {0.62, "#0f8f7a"}, {0.82, "#073730"}, {1.0, "#020c0b"}}),
        make("magenta-armour", QStringLiteral("Magenta armour"),
             {{0.0, "#12030f"}, {0.25, "#5a0f55"}, {0.42, "#c026b8"}, {0.5, "#f58cf0"}, {0.54, "#ffe6fd"},
              {0.62, "#9a1a93"}, {0.82, "#3a0a37"}, {1.0, "#0c020a"}}),
        make("sky", QStringLiteral("Sky"), {{0.0, "#1e5fc4"}, {0.6, "#8ec5ff"}, {1.0, "#ffffff"}}),
        make("sunset", QStringLiteral("Sunset"),
             {{0.0, "#2b1055"}, {0.4, "#d53369"}, {0.75, "#f9a03f"}, {1.0, "#ffe29a"}}),
        make("fire", QStringLiteral("Fire"),
             {{0.0, "#000000"}, {0.35, "#a80000"}, {0.7, "#ff7a00"}, {1.0, "#fff3a0"}}),
        make("rainbow", QStringLiteral("Rainbow"),
             {{0.0, "#ff0000"}, {0.1667, "#ffff00"}, {0.3333, "#00ff00"}, {0.5, "#00ffff"}, {0.6667, "#0000ff"},
              {0.8333, "#ff00ff"}, {1.0, "#ff0000"}}),
        make("black-white", QStringLiteral("Black to white"), {{0.0, "#000000"}, {1.0, "#ffffff"}}),
    };
    return presets;
}

GradientStops shadedStops(const QColor &color)
{
    const QColor c = color.toRgb();
    const auto toward = [&c](const QColor &to, double k) {
        return QColor::fromRgbF(float(c.redF() + (to.redF() - c.redF()) * k), float(c.greenF() + (to.greenF() - c.greenF()) * k),
                                float(c.blueF() + (to.blueF() - c.blueF()) * k));
    };
    return {{0.0, toward(Qt::white, 0.85)}, {0.3, c}, {0.8, toward(Qt::black, 0.6)}, {1.0, toward(Qt::black, 0.8)}};
}

double gradientPosition(const Gradient &gradient, int x, int y)
{
    const QPointF axis = gradient.to - gradient.from;
    const double length2 = axis.x() * axis.x() + axis.y() * axis.y();
    if (length2 <= 0.0)
        return 0.0;
    const double px = x + 0.5 - gradient.from.x(), py = y + 0.5 - gradient.from.y();
    double t = 0.0;
    switch (gradient.shape) {
    case GradientShape::Linear:
        t = (px * axis.x() + py * axis.y()) / length2;
        break;
    case GradientShape::Reflected:
        t = std::abs(px * axis.x() + py * axis.y()) / length2;
        break;
    case GradientShape::Radial:
        t = std::sqrt((px * px + py * py) / length2);
        break;
    case GradientShape::Conical: {
        // The angle from the line, clockwise as seen (y runs down), as a
        // fraction of a full turn.
        constexpr double kTwoPi = 6.283185307179586;
        const double angle = std::atan2(axis.x() * py - axis.y() * px, axis.x() * px + axis.y() * py);
        t = (angle < 0.0 ? angle + kTwoPi : angle) / kTwoPi;
        break;
    }
    }
    return std::clamp(t, 0.0, 1.0);
}

QHash<TileCoord, QImage> fillGradient(TileStore &store, const Selection &clip, const QRect &canvas,
                                      const Gradient &gradient)
{
    const QRect area = clip.isEmpty() ? canvas : (clip.bounds() & canvas);
    const QPointF axis = gradient.to - gradient.from;
    if (area.isEmpty() || (axis.x() == 0.0 && axis.y() == 0.0))
        return {};
    const GradientStops stops = normalizedStops(gradient.stops);

    // The ramp, worked out once: finer than any pixel step of a 16-bit ramp
    // across a screen, and mixed linearly between entries.
    constexpr int kSteps = 4096;
    std::vector<float> ramp(size_t(kSteps + 1) * 4);
    for (int i = 0; i <= kSteps; ++i) {
        const Pixel p = gradientPixel(stops, double(i) / kSteps);
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
        const double pos = gradientPosition(gradient, x, y) * kSteps;
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
