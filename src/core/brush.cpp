#include "brush.h"

#include "heal.h"

#include <algorithm>
#include <cmath>

namespace easeletch {

// --- Settings -----------------------------------------------------------------

double BrushSettings::diameterAt(double pressure) const
{
    const double p = std::clamp(pressure, 0.0, 1.0);
    const double least = std::clamp(minSize, 0.0, 1.0);
    const double d = pressureSize ? size * (least + (1.0 - least) * p) : size;
    return std::clamp(d, MinSize, MaxSize);
}

double BrushSettings::dabStrengthAt(double pressure) const
{
    const double p = std::clamp(pressure, 0.0, 1.0);
    return std::clamp(flow, 0.0, 1.0) * (pressureOpacity ? p : 1.0);
}

double BrushSettings::spacingAt(double pressure) const
{
    // A thin tip is spaced by its thickness, or it would leave gaps going sideways.
    const double thick = tip == BrushTip::Round ? 1.0 : std::clamp(aspect, 0.05, 1.0);
    const double step = std::max(0.5, std::clamp(spacing, 0.01, 2.0) * diameterAt(pressure) * thick);
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

// --- Paper --------------------------------------------------------------------

namespace {

quint32 hash2(int x, int y)
{
    quint32 h = quint32(x) * 0x9E3779B1u ^ quint32(y) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float unit(quint32 h)
{
    return float(h >> 8) * (1.0f / 16777216.0f);
}

// Smooth noise with bumps `scale` pixels across.
float bumps(int x, int y, float scale, int salt)
{
    const float fx = (float(x) + 0.5f) / scale, fy = (float(y) + 0.5f) / scale;
    const float flx = std::floor(fx), fly = std::floor(fy);
    const int ix = int(flx) + salt * 7919, iy = int(fly) - salt * 104729;
    float tx = fx - flx, ty = fy - fly;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = unit(hash2(ix, iy)), b = unit(hash2(ix + 1, iy));
    const float c = unit(hash2(ix, iy + 1)), d = unit(hash2(ix + 1, iy + 1));
    return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
}

} // namespace

float smoothNoise(int x, int y, double scaleX, double scaleY, int salt)
{
    const float fx = (float(x) + 0.5f) / float(std::max(scaleX, 1.0));
    const float fy = (float(y) + 0.5f) / float(std::max(scaleY, 1.0));
    const float flx = std::floor(fx), fly = std::floor(fy);
    const int ix = int(flx) + salt * 7919, iy = int(fly) - salt * 104729;
    float tx = fx - flx, ty = fy - fly;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = unit(hash2(ix, iy)), b = unit(hash2(ix + 1, iy));
    const float c = unit(hash2(ix, iy + 1)), d = unit(hash2(ix + 1, iy + 1));
    return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
}

float paperTooth(int x, int y, double size)
{
    // Broad bumps with finer ones on them.
    const float s = float(std::clamp(size, 1.0, 64.0));
    return 0.65f * bumps(x, y, s, 0) + 0.35f * bumps(x, y, std::max(1.0f, s * 0.5f), 1);
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
    m_canvas = bounds;
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
    m_jitterStep = 0;
    const bool shaped = settings.tip != BrushTip::Round && !settings.pixel;
    m_needsHeading = !settings.pixel && ((shaped && settings.followStroke) || settings.streaks > 0.0);
    m_hasHeading = false;
    m_hasPending = false;
    m_headingFrom = first.pos;
    m_strokeSeed = hash2(int(std::lround(first.pos.x() * 16.0)), int(std::lround(first.pos.y() * 16.0)));

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
    if (m_hasPending) {
        // A click that never moved: there's no direction to turn to.
        m_hasPending = false;
        const double a = m_settings.angle * 3.141592653589793 / 180.0;
        paintOne(m_pending, QPointF(std::cos(a), std::sin(a)));
    }
    if (m_mode == BrushMode::Heal)
        finishHeal();

    for (const TileCoord c : std::as_const(m_touched))
        before.insert(c, m_before.tile(c));

    m_target = nullptr;
    m_before = TileStore();
    m_mask.clear();
    m_touched.clear();
    m_carry.clear();
    return before;
}

namespace {

// Pixel mode centres dabs on pixel centres.
QPointF dabCentre(const QPointF &pos, bool pixel)
{
    return pixel ? QPointF(std::floor(pos.x()) + 0.5, std::floor(pos.y()) + 0.5) : pos;
}

} // namespace

void BrushStroke::paintDabs(const QList<StrokeSample> &dabs)
{
    const double jitter = m_settings.pixel ? 0.0 : std::clamp(m_settings.jitter, 0.0, 1.0);
    const double fixed = m_settings.angle * 3.141592653589793 / 180.0;
    const QPointF held(std::cos(fixed), std::sin(fixed)); // the wide side, when it doesn't turn
    const bool turns = m_needsHeading && m_settings.tip != BrushTip::Round && m_settings.followStroke;

    for (StrokeSample d : dabs) {
        if (m_needsHeading) {
            // The way the stroke is going, steadied over a few dabs so the
            // tip turns through a corner rather than snapping round it.
            const QPointF step = d.pos - m_headingFrom;
            const double length = std::hypot(step.x(), step.y());
            if (length > 1e-6) {
                const QPointF unit = step / length;
                QPointF h = m_hasHeading ? m_heading * 0.6 + unit * 0.4 : unit;
                const double n = std::hypot(h.x(), h.y());
                m_heading = n > 1e-6 ? h / n : unit;
                m_hasHeading = true;
                m_headingFrom = d.pos;
            }
            if (!m_hasHeading) {
                // Nowhere yet: keep the dab until the stroke shows its direction.
                m_pending = d;
                m_hasPending = true;
                continue;
            }
        }
        // The tip's wide side: across the stroke if it turns with it.
        const QPointF across(-m_heading.y(), m_heading.x());
        const QPointF wide = turns ? across : (m_needsHeading && m_settings.tip == BrushTip::Round) ? across : held;
        if (m_hasPending) {
            m_hasPending = false;
            paintOne(m_pending, wide);
        }
        if (jitter > 0.0) {
            // Off the line by a repeatable amount: the same stroke scatters the
            // same way, and its mirror images scatter with it.
            const quint32 h = hash2(int(m_jitterStep), 0x51ED);
            ++m_jitterStep;
            const double angle = unit(h) * 6.283185307179586;
            const double reach = unit(hash2(int(h), 7)) * jitter * m_settings.diameterAt(d.pressure);
            d.pos += QPointF(std::cos(angle), std::sin(angle)) * reach;
        }
        paintOne(d, wide);
    }
}

void BrushStroke::paintOne(const StrokeSample &d, const QPointF &wide)
{
    if (smears()) {
        smudgeDab(d, wide);
        return;
    }
    paintDab(d, wide);
    const bool mirrored = m_symmetry != Symmetry::Off && (m_mode == BrushMode::Paint || m_mode == BrushMode::Erase);
    if (!mirrored)
        return;
    const bool acrossX = m_symmetry == Symmetry::LeftRight || m_symmetry == Symmetry::Quarters;
    const bool acrossY = m_symmetry == Symmetry::TopBottom || m_symmetry == Symmetry::Quarters;
    // The dab's centre is mirrored as it's painted (on its pixel, in
    // pixel mode), so the two sides match pixel for pixel. Its tip is
    // mirrored with it.
    const QPointF c = dabCentre(d.pos, m_settings.pixel);
    const QPointF flipped(2.0 * m_axis.x() - c.x(), 2.0 * m_axis.y() - c.y());
    if (acrossX)
        paintDab({QPointF(flipped.x(), c.y()), d.pressure}, QPointF(-wide.x(), wide.y()));
    if (acrossY)
        paintDab({QPointF(c.x(), flipped.y()), d.pressure}, QPointF(wide.x(), -wide.y()));
    if (acrossX && acrossY)
        paintDab({flipped, d.pressure}, QPointF(-wide.x(), -wide.y()));
}

float BrushStroke::tipCoverage(float dx, float dy, double radius, const QPointF &wide) const
{
    if (m_settings.pixel || m_settings.tip == BrushTip::Round)
        return coverageAt(std::sqrt(dx * dx + dy * dy), radius);

    // In the tip's own terms: u along its wide side, v across it.
    const float wx = float(wide.x()), wy = float(wide.y());
    const float u = std::abs(dx * wx + dy * wy), v = std::abs(-dx * wy + dy * wx);
    const float a = float(radius);
    const float b = std::max(0.5f, a * float(std::clamp(m_settings.aspect, 0.05, 1.0)));
    const float h = float(std::clamp(m_settings.hardness, 0.0, 1.0));

    float outside; // distance beyond the tip's edge, in pixels (negative inside)
    float depth;   // 0 at the centre, 1 at the edge
    if (m_settings.tip == BrushTip::Flat) {
        // A bar with its corners a little rounded.
        const float r = b * 0.35f;
        const float qx = u - (a - r), qy = v - (b - r);
        const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        outside = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
        depth = std::max(u / a, v / b);
    } else {
        const float k = std::sqrt((u / a) * (u / a) + (v / b) * (v / b));
        if (k <= 1e-6f)
            return 1.0f;
        // How far a step of k is in pixels, here.
        const float gu = u / (a * a), gv = v / (b * b);
        const float slope = std::sqrt(gu * gu + gv * gv) / k;
        outside = (k - 1.0f) / std::max(slope, 1e-6f);
        depth = k;
    }
    const float rim = std::clamp(0.5f - outside, 0.0f, 1.0f); // antialiased over a pixel
    if (rim <= 0.0f)
        return 0.0f;
    if (h >= 1.0f || depth <= h)
        return rim;
    const float t = std::clamp((depth - h) / std::max(1.0f - h, 1e-6f), 0.0f, 1.0f);
    return std::min(rim, 1.0f - t * t * (3.0f - 2.0f * t));
}

float BrushStroke::coverageAt(double dist, double radius) const
{
    if (m_settings.pixel)
        return dist <= radius + 1e-9 ? 1.0f : 0.0f;
    return dabCoverage(dist, radius, m_settings.hardness);
}

void BrushStroke::paintDab(const StrokeSample &dab, const QPointF &wide)
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
    // Paper: how high a point of it has to be to take colour from this dab.
    // A light touch only marks the high points; a heavy one fills most of it.
    const float grain = m_settings.pixel ? 0.0f : float(std::clamp(m_settings.grain, 0.0, 1.0));
    const float toothNeeded = grain * (0.85f - 0.5f * float(std::clamp(dab.pressure, 0.0, 1.0)));
    constexpr float kToothEdge = 0.12f; // how gradually a slope takes colour
    // Bristles: where across the tip a pixel is decides how much paint its
    // bristle carries. The pattern is the stroke's, so it runs along it.
    const float streaks = m_settings.pixel ? 0.0f : float(std::clamp(m_settings.streaks, 0.0, 1.0));
    // How much paint a bristle needs to leave a mark: next to none pressed
    // hard, most of a load at a light touch.
    const float bristleNeeded = streaks * (0.95f - 0.75f * float(std::clamp(dab.pressure, 0.0, 1.0)));
    const float wx = float(wide.x()), wy = float(wide.y());
    const float bristle = float(std::clamp(m_settings.size / 14.0, 1.2, 4.0)); // across one, in pixels

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
                if (dx * dx + dy * dy >= reach * reach)
                    continue;
                // A feathered selection lets that much of the stroke through.
                const float clip = m_clip.isEmpty() ? 1.0f : m_clip.coverage(x, y);
                if (clip <= 0.0f)
                    continue;
                float a = tipCoverage(dx, dy, radius, wide) * strength;
                if (a <= 0.0f)
                    continue;
                if (streaks > 0.0f) {
                    const float along = (dx * wx + dy * wy) / bristle;
                    const float fl = std::floor(along);
                    float f = along - fl;
                    f = f * f * (3.0f - 2.0f * f);
                    const float p0 = unit(hash2(int(fl), int(m_strokeSeed & 0xFFFF)));
                    const float p1 = unit(hash2(int(fl) + 1, int(m_strokeSeed & 0xFFFF)));
                    const float paint = p0 + (p1 - p0) * f; // what this bristle holds
                    const float t = std::clamp((paint - bristleNeeded) / 0.3f + 1.0f, 0.0f, 1.0f);
                    // Thin bristles leave less, down to nothing; none leaves more.
                    a *= (1.0f - streaks * 0.35f * (1.0f - paint)) * (t * t * (3.0f - 2.0f * t));
                    if (a <= 0.0f)
                        continue;
                }
                if (grain > 0.0f) {
                    const float t = std::clamp(
                        (paperTooth(x, y, m_settings.grainSize) - (toothNeeded - kToothEdge)) / (2.0f * kToothEdge),
                        0.0f, 1.0f);
                    a *= t * t * (3.0f - 2.0f * t);
                    if (a <= 0.0f)
                        continue;
                }
                // Clone: nothing to copy from beyond the edge of the canvas.
                if (m_mode == BrushMode::Clone && !m_canvas.contains(x + m_cloneOffset.x(), y + m_cloneOffset.y()))
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
                } else if (m_mode == BrushMode::Clone) {
                    // A straight copy, transparency included: what's there
                    // turns into what's at the source, by the coverage.
                    const Pixel s = m_before.pixel(x + m_cloneOffset.x(), y + m_cloneOffset.y());
                    out[i] = makePixel(dr + (float(s.r) - dr) * cov, dg + (float(s.g) - dg) * cov,
                                       db + (float(s.b) - db) * cov, da + (float(s.a) - da) * cov);
                } else if (m_mode == BrushMode::Heal) {
                    // Until the stroke ends, a grey veil shows what's marked.
                    const float k = cov * 0.5f;
                    out[i] = makePixel(dr + (0.2f - dr) * k, dg + (0.2f - dg) * k, db + (0.2f - db) * k,
                                       da + (1.0f - da) * k);
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

void BrushStroke::finishHeal()
{
    constexpr int N = TileStore::TileSize;
    // The veil comes off, then what it covered is healed in one go.
    for (const TileCoord c : std::as_const(m_touched))
        m_target->setTile(c, m_before.tile(c));

    QRect area;
    for (auto it = m_mask.cbegin(); it != m_mask.cend(); ++it) {
        const QRect tr = TileStore::tileRect(it.key());
        const std::vector<float> &mask = it.value();
        for (int i = 0; i < N * N; ++i)
            if (mask[size_t(i)] > 0.0f)
                area |= QRect(tr.left() + i % N, tr.top() + i / N, 1, 1);
    }
    area &= m_bounds;
    if (area.isEmpty())
        return;
    const float opacity = float(std::clamp(m_settings.opacity, 0.0, 1.0));
    std::vector<float> coverage(size_t(area.width()) * size_t(area.height()), 0.0f);
    for (auto it = m_mask.cbegin(); it != m_mask.cend(); ++it) {
        const QRect tr = TileStore::tileRect(it.key());
        const QRect part = tr & area;
        const std::vector<float> &mask = it.value();
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                const float clip = m_clip.isEmpty() ? 1.0f : m_clip.coverage(x, y);
                coverage[size_t(y - area.top()) * size_t(area.width()) + size_t(x - area.left())] =
                    mask[size_t((y - tr.top()) * N + (x - tr.left()))] * opacity * clip;
            }
    }
    healArea(*m_target, m_canvas, area, coverage);
}

void BrushStroke::smudgeDab(const StrokeSample &dab, const QPointF &wide)
{
    // Each dab blends the colour carried from earlier dabs into the canvas,
    // then picks up some of what it passed over. Strength (the opacity setting)
    // sets how far colour is dragged: 1 smears it all the way, 0 does nothing.
    constexpr int N = TileStore::TileSize;
    const double radius = std::min(std::max(0.5, m_settings.diameterAt(dab.pressure) * 0.5),
                                   double(m_carryHalf - 1));
    // A knife (the brush with smear) drags by its smear; Smudge by its opacity.
    const double drag = m_mode == BrushMode::Smudge ? m_settings.opacity : m_settings.smear;
    const float strength = float(std::clamp(drag, 0.0, 1.0))
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
                const float a = tipCoverage(float(dx), float(dy), radius, wide);
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
