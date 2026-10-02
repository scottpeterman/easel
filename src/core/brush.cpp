#include "brush.h"

#include <algorithm>
#include <cmath>

namespace easeletch {

// --- Settings -----------------------------------------------------------------

double BrushSettings::diameterAt(double pressure) const
{
    const double p = std::clamp(pressure, 0.0, 1.0);
    const double d = pressureSize ? size * p : size;
    return std::clamp(d, MinSize, MaxSize);
}

double BrushSettings::dabStrengthAt(double pressure) const
{
    const double p = std::clamp(pressure, 0.0, 1.0);
    return std::clamp(flow, 0.0, 1.0) * (pressureOpacity ? p : 1.0);
}

double BrushSettings::spacingAt(double pressure) const
{
    const double step = std::max(0.5, std::clamp(spacing, 0.01, 2.0) * diameterAt(pressure));
    // Pixel mode snaps dabs to pixels: step at most a pixel so lines stay unbroken.
    return pixel ? std::min(step, 1.0) : step;
}

float dabCoverage(double distance, double radius, double hardness)
{
    // One-pixel antialiased rim of a hard disc.
    const double rim = std::clamp(radius + 0.5 - distance, 0.0, 1.0);
    if (rim <= 0.0)
        return 0.0f;
    const double h = std::clamp(hardness, 0.0, 1.0);
    const double inner = h * radius;
    if (h >= 1.0 || distance <= inner)
        return float(rim);
    // Smoothstep from full at `inner` to zero at the radius.
    const double t = std::clamp((distance - inner) / std::max(radius - inner, 1e-6), 0.0, 1.0);
    const double soft = 1.0 - t * t * (3.0 - 2.0 * t);
    return float(std::min(rim, soft));
}

// --- Stabilizer ---------------------------------------------------------------

void Stabilizer::setStrength(double strength)
{
    m_window = 1 + int(std::lround(std::clamp(strength, 0.0, 1.0) * 31.0));
}

void Stabilizer::reset(const StrokeSample &first)
{
    m_samples.clear();
    m_samples.push_back(first);
}

StrokeSample Stabilizer::add(const StrokeSample &raw)
{
    m_samples.push_back(raw);
    while (int(m_samples.size()) > m_window)
        m_samples.pop_front();

    StrokeSample avg{QPointF(0, 0), 0.0};
    for (const StrokeSample &s : m_samples) {
        avg.pos += s.pos;
        avg.pressure += s.pressure;
    }
    const double n = double(m_samples.size());
    avg.pos /= n;
    avg.pressure /= n;
    return avg;
}

QList<StrokeSample> Stabilizer::flush()
{
    QList<StrokeSample> out;
    if (m_samples.empty() || m_window <= 1)
        return out;
    const StrokeSample last = m_samples.back();
    for (int i = 0; i < m_window; ++i)
        out.append(add(last));
    return out;
}

// --- Dab spacing --------------------------------------------------------------

void DabSpacer::begin(const StrokeSample &first, QList<StrokeSample> &dabs)
{
    m_last = first;
    m_sinceLastDab = 0.0;
    dabs.append(first);
}

void DabSpacer::moveTo(const StrokeSample &to, const BrushSettings &settings,
                       QList<StrokeSample> &dabs)
{
    const QPointF delta = to.pos - m_last.pos;
    const double length = std::hypot(delta.x(), delta.y());
    if (length <= 0.0) {
        m_last.pressure = to.pressure;
        return;
    }

    // Distance along this segment of the next dab.
    double step = settings.spacingAt(m_last.pressure);
    double at = step - m_sinceLastDab;
    double lastDabAt = -1.0;
    while (at <= length) {
        const double t = at / length;
        const StrokeSample dab{m_last.pos + delta * t,
                               m_last.pressure + (to.pressure - m_last.pressure) * t};
        dabs.append(dab);
        lastDabAt = at;
        step = settings.spacingAt(dab.pressure);
        at += step;
    }
    m_sinceLastDab = lastDabAt < 0.0 ? m_sinceLastDab + length : length - lastDabAt;
    m_last = to;
}

// --- Stroke -------------------------------------------------------------------

void BrushStroke::begin(TileStore *target, const QRect &bounds, const BrushSettings &settings,
                        const QColor &color, BrushMode mode, const StrokeSample &first,
                        const Selection &clip)
{
    m_target = target;
    m_before = target->snapshot();
    m_clip = clip;
    m_bounds = clip.isEmpty() ? bounds : (bounds & clip.bounds());
    m_touched.clear();
    m_carryHalf = int(std::ceil(settings.diameterAt(1.0) / 2.0)) + 2;
    const size_t cells = size_t(2 * m_carryHalf + 1) * size_t(2 * m_carryHalf + 1);
    m_carry.assign(cells, {0, 0, 0, 0});
    m_carryLoaded.assign(cells, 0);
    m_settings = settings;
    m_mode = mode;
    m_mask.clear();
    // Pre-size the mask table: growing it mid-stroke rehashes on the UI thread
    // and showed up as 5-10 ms hitches at 512 and 1024 touched tiles.
    m_mask.reserve(8192);
    m_dabCount = 0;

    const QColor c = color.toRgb();
    m_color[0] = srgbToLinear(float(c.redF()));
    m_color[1] = srgbToLinear(float(c.greenF()));
    m_color[2] = srgbToLinear(float(c.blueF()));
    m_colorAlpha = float(c.alphaF());

    m_stabilizer.setStrength(settings.stabilizer);
    m_stabilizer.reset(first);

    QList<StrokeSample> dabs;
    m_spacer.begin(first, dabs);
    paintDabs(dabs);
}

void BrushStroke::moveTo(const StrokeSample &raw)
{
    if (!m_target)
        return;
    QList<StrokeSample> dabs;
    m_spacer.moveTo(m_stabilizer.add(raw), m_settings, dabs);
    paintDabs(dabs);
}

QHash<TileCoord, QImage> BrushStroke::end()
{
    QHash<TileCoord, QImage> before;
    if (!m_target)
        return before;

    QList<StrokeSample> dabs;
    for (const StrokeSample &s : m_stabilizer.flush())
        m_spacer.moveTo(s, m_settings, dabs);
    paintDabs(dabs);

    for (const TileCoord c : std::as_const(m_touched))
        before.insert(c, m_before.tile(c));

    m_target = nullptr;
    m_before = TileStore();
    m_mask.clear();
    m_touched.clear();
    m_carry.clear();
    return before;
}

void BrushStroke::paintDabs(const QList<StrokeSample> &dabs)
{
    for (const StrokeSample &d : dabs) {
        if (m_mode == BrushMode::Smudge)
            smudgeDab(d);
        else
            paintDab(d);
    }
}

float BrushStroke::coverageAt(double dist, double radius) const
{
    if (m_settings.pixel)
        return dist <= radius + 1e-9 ? 1.0f : 0.0f;
    return dabCoverage(dist, radius, m_settings.hardness);
}

namespace {

// Pixel mode centres dabs on pixel centres.
QPointF dabCentre(const QPointF &pos, bool pixel)
{
    return pixel ? QPointF(std::floor(pos.x()) + 0.5, std::floor(pos.y()) + 0.5) : pos;
}

} // namespace

void BrushStroke::paintDab(const StrokeSample &dab)
{
    constexpr int N = TileStore::TileSize;
    const double radius = std::max(0.5, m_settings.diameterAt(dab.pressure) * 0.5);
    const float strength = float(m_settings.dabStrengthAt(dab.pressure));
    if (strength <= 0.0f)
        return;
    ++m_dabCount;

    const QPointF centre = dabCentre(dab.pos, m_settings.pixel);
    const double cx = centre.x(), cy = centre.y();
    const QRect box = QRect(QPoint(int(std::floor(cx - radius - 1.0)), int(std::floor(cy - radius - 1.0))),
                            QPoint(int(std::ceil(cx + radius + 1.0)), int(std::ceil(cy + radius + 1.0))))
                      & m_bounds;
    if (box.isEmpty())
        return;

    const float opacity = float(std::clamp(m_settings.opacity, 0.0, 1.0));
    const Pixel def = m_before.defaultPixel();
    const float reach = float(radius + 1.0);

    for (const TileCoord c : TileStore::tilesIntersecting(box)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & box;

        std::vector<float> &mask = m_mask[c];
        if (mask.empty())
            mask.assign(size_t(N) * N, 0.0f);
        m_touched.insert(c);

        const QImage beforeImg = m_before.tile(c);
        const auto *before = beforeImg.isNull() ? nullptr
                                                : reinterpret_cast<const Pixel *>(beforeImg.constBits());
        auto *out = reinterpret_cast<Pixel *>(m_target->writableTile(c).bits());

        for (int y = part.top(); y <= part.bottom(); ++y) {
            const float dy = float(y + 0.5 - cy);
            const int row = (y - tr.top()) * N;
            for (int x = part.left(); x <= part.right(); ++x) {
                const float dx = float(x + 0.5 - cx);
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= reach)
                    continue;
                // A feathered selection lets that much of the stroke through.
                const float clip = m_clip.isEmpty() ? 1.0f : m_clip.coverage(x, y);
                if (clip <= 0.0f)
                    continue;
                const float a = coverageAt(dist, radius) * strength;
                if (a <= 0.0f)
                    continue;

                const int i = row + (x - tr.left());
                float &m = mask[size_t(i)];
                m += a * (1.0f - m);
                const float cov = m * opacity * clip;

                const Pixel &d = before ? before[i] : def;
                const float dr = float(d.r), dg = float(d.g), db = float(d.b), da = float(d.a);
                if (m_mode == BrushMode::Erase) {
                    const float k = 1.0f - cov;
                    out[i] = makePixel(dr * k, dg * k, db * k, da * k);
                } else {
                    const float sa = m_colorAlpha * cov; // source alpha
                    const float k = 1.0f - sa;
                    out[i] = makePixel(m_color[0] * sa + dr * k, m_color[1] * sa + dg * k,
                                       m_color[2] * sa + db * k, sa + da * k);
                }
            }
        }
    }
}

void BrushStroke::smudgeDab(const StrokeSample &dab)
{
    // Each dab blends the colour carried from earlier dabs into the canvas,
    // then picks up some of what it passed over. Strength (the opacity setting)
    // sets how far colour is dragged: 1 smears it all the way, 0 does nothing.
    constexpr int N = TileStore::TileSize;
    const double radius = std::min(std::max(0.5, m_settings.diameterAt(dab.pressure) * 0.5),
                                   double(m_carryHalf - 1));
    const float strength = float(std::clamp(m_settings.opacity, 0.0, 1.0))
                           * float(m_settings.pressureOpacity ? std::clamp(dab.pressure, 0.0, 1.0) : 1.0);
    ++m_dabCount;

    const QPointF centre = dabCentre(dab.pos, m_settings.pixel);
    const int ix = int(std::floor(centre.x())), iy = int(std::floor(centre.y()));
    const int reach = int(std::ceil(radius)) + 1;
    const QRect box = QRect(ix - reach, iy - reach, 2 * reach + 1, 2 * reach + 1) & m_bounds;
    if (box.isEmpty())
        return;
    const int side = 2 * m_carryHalf + 1;

    for (const TileCoord c : TileStore::tilesIntersecting(box)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & box;
        m_touched.insert(c);
        auto *px = reinterpret_cast<Pixel *>(m_target->writableTile(c).bits());

        for (int y = part.top(); y <= part.bottom(); ++y) {
            const double dy = y + 0.5 - centre.y();
            for (int x = part.left(); x <= part.right(); ++x) {
                const float clip = m_clip.isEmpty() ? 1.0f : m_clip.coverage(x, y);
                if (clip <= 0.0f)
                    continue;
                const double dx = x + 0.5 - centre.x();
                const float a = coverageAt(std::sqrt(dx * dx + dy * dy), radius);
                if (a <= 0.0f)
                    continue;

                Pixel &p = px[(y - tr.top()) * N + (x - tr.left())];
                const float cur[4] = {float(p.r), float(p.g), float(p.b), float(p.a)};
                const size_t cell = size_t(y - iy + m_carryHalf) * size_t(side) + size_t(x - ix + m_carryHalf);
                auto &carry = m_carry[cell];
                if (!m_carryLoaded[cell]) {
                    // First time the brush covers this part of itself (the
                    // first dab, or the brush grew with pressure): pick up only.
                    carry = {cur[0], cur[1], cur[2], cur[3]};
                    m_carryLoaded[cell] = 1;
                    continue;
                }
                const float t = a * strength * clip;
                float out[4];
                for (int k = 0; k < 4; ++k) {
                    out[k] = cur[k] + (carry[size_t(k)] - cur[k]) * t;
                    carry[size_t(k)] += (cur[k] - carry[size_t(k)]) * a * (1.0f - strength);
                }
                p = makePixel(out[0], out[1], out[2], out[3]);
            }
        }
    }
}

} // namespace easeletch
