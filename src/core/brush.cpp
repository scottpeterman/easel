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
    // A cluster of marks is spaced by the cluster, whatever shape its marks are.
    const double thick = tip == BrushTip::Round || tip == BrushTip::Fan || marks > 1 ? 1.0 : std::clamp(aspect, 0.05, 1.0);
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
    m_paint.clear();
    m_multiColor = mode == BrushMode::Paint && !settings.pixel && settings.smear <= 0.0
                   && (settings.valueJitter > 0.0 || settings.hueJitter > 0.0 || settings.edge > 0.0);
    if (m_multiColor)
        m_paint.reserve(8192);
    m_dabCount = 0;
    m_jitterStep = 0;
    const bool shaped = settings.tip != BrushTip::Round && !settings.pixel;
    m_needsHeading = !settings.pixel
                     && ((shaped && settings.followStroke) || settings.streaks > 0.0
                         || (settings.edge > 0.0 && mode == BrushMode::Paint)
                         || (settings.grain > 0.0 && settings.grainStretch > 1.0));
    m_hasHeading = false;
    m_hasPending = false;
    m_headingFrom = first.pos;
    m_strokeSeed = hash2(int(std::lround(first.pos.x() * 16.0)), int(std::lround(first.pos.y() * 16.0)));

    const QColor c = color.toRgb();
    m_baseColor = c;
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
    m_paint.clear();
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
    QList<Mark> marks;
    marksFor(d, wide, marks);
    const bool mirrored = m_symmetry != Symmetry::Off && (m_mode == BrushMode::Paint || m_mode == BrushMode::Erase);
    const bool acrossX = m_symmetry == Symmetry::LeftRight || m_symmetry == Symmetry::Quarters;
    const bool acrossY = m_symmetry == Symmetry::TopBottom || m_symmetry == Symmetry::Quarters;
    for (const Mark &m : std::as_const(marks)) {
        paintMark(m);
        if (!mirrored)
            continue;
        // The mark's centre is mirrored as it's painted (on its pixel, in
        // pixel mode), so the two sides match pixel for pixel. Its tip is
        // mirrored with it.
        const QPointF c = dabCentre(m.pos, m_settings.pixel);
        const QPointF flipped(2.0 * m_axis.x() - c.x(), 2.0 * m_axis.y() - c.y());
        Mark mm = m;
        if (acrossX) {
            mm.pos = QPointF(flipped.x(), c.y());
            mm.wide = QPointF(-m.wide.x(), m.wide.y());
            paintMark(mm);
        }
        if (acrossY) {
            mm.pos = QPointF(c.x(), flipped.y());
            mm.wide = QPointF(m.wide.x(), -m.wide.y());
            paintMark(mm);
        }
        if (acrossX && acrossY) {
            mm.pos = flipped;
            mm.wide = QPointF(-m.wide.x(), -m.wide.y());
            paintMark(mm);
        }
    }
}

void BrushStroke::shadeColor(double dv, double dh, double desaturate, float out[3]) const
{
    float h = 0, sat = 0, v = 0;
    m_baseColor.getHsvF(&h, &sat, &v);
    v = float(dv >= 0.0 ? v + (1.0 - v) * dv : v * (1.0 + dv));
    sat = float(sat * (1.0 - std::clamp(desaturate, 0.0, 1.0)));
    if (h >= 0.0f) // greys have no hue to turn
        h = float(std::fmod(h + dh / 12.0 + 1.0, 1.0));
    else
        h = 0.0f;
    const QColor c = QColor::fromHsvF(h, std::clamp(sat, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f));
    out[0] = srgbToLinear(float(c.redF()));
    out[1] = srgbToLinear(float(c.greenF()));
    out[2] = srgbToLinear(float(c.blueF()));
}

void BrushStroke::marksFor(const StrokeSample &d, const QPointF &wide, QList<Mark> &out)
{
    const double p = std::clamp(d.pressure, 0.0, 1.0);
    const double radius = std::max(0.5, m_settings.diameterAt(d.pressure) * 0.5);
    const bool pixel = m_settings.pixel;
    const double vj = std::clamp(m_settings.valueJitter, 0.0, 1.0);
    const double hj = std::clamp(m_settings.hueJitter, 0.0, 1.0);
    const auto plain = [&](Mark &m) { std::copy(m_color, m_color + 3, m.color); };
    // Repeatable per stroke: the same stroke scatters the same way.
    const auto roll = [&](int salt) {
        return unit(hash2(int(m_jitterStep), int(m_strokeSeed & 0xFFFF) * 31 + salt));
    };

    const int count = pixel ? 1 : std::clamp(m_settings.marks, 1, 64);
    if (count <= 1) {
        Mark m{d.pos, d.pressure, radius, wide};
        if (m_multiColor && (vj > 0.0 || hj > 0.0)) {
            shadeColor((roll(5) * 2.0 - 1.0) * vj, (roll(6) * 2.0 - 1.0) * hj, 0.0, m.color);
            ++m_jitterStep;
        } else {
            plain(m);
        }
        out.append(m);
    } else {
        // A cluster: fewer marks at a light touch, the full count pressed hard.
        const int n = std::max(1, int(std::lround(count * (0.3 + 0.7 * p))));
        const double markR = std::max(0.5, radius * std::clamp(m_settings.markSize, 0.05, 1.0));
        const double sizeJ = std::clamp(m_settings.sizeJitter, 0.0, 0.95);
        const double turnJ = std::clamp(m_settings.angleJitter, 0.0, 1.0) * 3.141592653589793;
        for (int i = 0; i < n; ++i) {
            Mark m;
            m.pressure = d.pressure;
            m.radius = std::max(0.5, markR * (1.0 - sizeJ * roll(3)));
            // Evenly over the disc the marks stay inside.
            const double reach = std::max(0.0, radius - m.radius);
            const double dist = std::sqrt(roll(1)) * reach;
            const double ang = roll(2) * 6.283185307179586;
            m.pos = d.pos + QPointF(std::cos(ang), std::sin(ang)) * dist;
            const double turn = (roll(4) * 2.0 - 1.0) * turnJ;
            const double ct = std::cos(turn), st = std::sin(turn);
            m.wide = QPointF(wide.x() * ct - wide.y() * st, wide.x() * st + wide.y() * ct);
            if (m_multiColor)
                shadeColor((roll(5) * 2.0 - 1.0) * vj, (roll(6) * 2.0 - 1.0) * hj, 0.0, m.color);
            else
                plain(m);
            ++m_jitterStep;
            out.append(m);
        }
    }

    if (!pixel && m_settings.edge > 0.0 && m_mode == BrushMode::Paint && m_settings.marks <= 1) {
        // Bark: a thin lit edge beside the groove, on the side facing the
        // light, wandering a little in and out as a real edge does.
        const double la = m_settings.lightAngle * 3.141592653589793 / 180.0;
        const QPointF toLight(std::cos(la), std::sin(la));
        const double side = wide.x() * toLight.x() + wide.y() * toLight.y() >= 0.0 ? 1.0 : -1.0;
        const double er = std::max(0.5, radius * std::clamp(m_settings.edgeWidth, 0.05, 1.0));
        const double wobble = 1.0 + 0.35 * (smoothNoise(int(m_jitterStep), 0, 5.0, 1.0, int(m_strokeSeed & 0xFF)) * 2.0 - 1.0);
        ++m_jitterStep;
        Mark e;
        e.pressure = d.pressure;
        e.radius = er;
        e.wide = wide;
        e.pos = d.pos + wide * (side * (radius + er * 0.8) * wobble);
        shadeColor(std::clamp(m_settings.edge, 0.0, 1.0), 0.0, 0.7 * m_settings.edge, e.color);
        out.append(e);
    }
}

float BrushStroke::tipCoverage(float dx, float dy, double radius, const QPointF &wide) const
{
    if (m_settings.pixel || m_settings.tip == BrushTip::Round)
        return coverageAt(std::sqrt(dx * dx + dy * dy), radius);

    // In the tip's own terms: u along its wide side, v across it.
    const float wx = float(wide.x()), wy = float(wide.y());
    if (m_settings.tip == BrushTip::Fan)
        return fanCoverage(dx * wx + dy * wy, -dx * wy + dy * wx, float(radius));
    const float u = std::abs(dx * wx + dy * wy), v = std::abs(-dx * wy + dy * wx);
    const float a = float(radius);
    const float b = std::max(0.5f, a * float(std::clamp(m_settings.aspect, 0.05, 1.0)));
    const float h = float(std::clamp(m_settings.hardness, 0.0, 1.0));

    float outside; // distance beyond the tip's edge, in pixels (negative inside)
    float depth;   // 0 at the centre, 1 at the edge
    if (m_settings.tip == BrushTip::Leaf) {
        // Pointed at both ends: as wide across as b in the middle, narrowing
        // to nothing at either end of its length.
        const float t = std::min(u / a, 1.0f);
        const float half = b * (1.0f - t * t);
        outside = std::max(v - half, u - a);
        depth = std::max(t, half > 1e-3f ? v / half : 1.0f);
    } else if (m_settings.tip == BrushTip::Flat) {
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

float BrushStroke::fanCoverage(float u, float v, float a) const
{
    // The bristles spread from the ferrule, well behind the dab (towards -v),
    // and touch down in an arc on the far side: what a tap of a fan leaves.
    constexpr float kHalfAngle = 0.62f; // about 35 degrees each side
    constexpr int kBristles = 19;
    const float py = v + 1.55f * a; // from the ferrule
    const float rho = std::sqrt(u * u + py * py);
    if (rho < 1e-3f)
        return 0.0f;
    const float theta = std::atan2(u, py);
    if (std::abs(theta) > kHalfAngle)
        return 0.0f;
    // Which bristle, and how far across it: each is a line with a gap beside it.
    const float f = (theta / kHalfAngle * 0.5f + 0.5f) * float(kBristles);
    const int b = std::clamp(int(f), 0, kBristles - 1);
    const float across = std::abs((f - float(b)) - 0.5f) * 2.0f; // 0 middle, 1 edge
    const float body = std::clamp((0.92f - across) / 0.3f, 0.0f, 1.0f);
    if (body <= 0.0f)
        return 0.0f;
    // Each bristle reaches its own length, and only its end touches down.
    const quint32 h = hash2(b, int(m_strokeSeed & 0xFFF));
    const float reach = a * (2.1f + 0.65f * unit(h)); // tip
    // Only the last stretch touches: a short, thick mark, not a long line.
    const float from = reach - a * (0.45f + 0.4f * unit(hash2(int(h), 3)));
    const float hard = float(std::clamp(m_settings.hardness, 0.0, 1.0));
    const float soft = 1.0f + (1.0f - hard) * a * 0.4f;
    const float inTip = std::clamp((reach - rho) / soft + 0.5f, 0.0f, 1.0f);
    const float inHeel = std::clamp((rho - from) / (a * 0.35f), 0.0f, 1.0f);
    return body * inTip * inHeel;
}

float BrushStroke::coverageAt(double dist, double radius) const
{
    if (m_settings.pixel)
        return dist <= radius + 1e-9 ? 1.0f : 0.0f;
    return dabCoverage(dist, radius, m_settings.hardness);
}

void BrushStroke::paintMark(const Mark &mark)
{
    constexpr int N = TileStore::TileSize;
    const StrokeSample dab{mark.pos, mark.pressure};
    const QPointF &wide = mark.wide;
    const double radius = mark.radius;
    const float strength = float(m_settings.dabStrengthAt(dab.pressure));
    if (strength <= 0.0f)
        return;
    ++m_dabCount;

    const QPointF centre = dabCentre(dab.pos, m_settings.pixel);
    const double cx = centre.x(), cy = centre.y();
    // A fan's bristles splay out past its size; every other tip stays inside it.
    const double extent = radius * (m_settings.tip == BrushTip::Fan && !m_settings.pixel ? 1.8 : 1.0);
    const QRect box = QRect(QPoint(int(std::floor(cx - extent - 1.0)), int(std::floor(cy - extent - 1.0))),
                            QPoint(int(std::ceil(cx + extent + 1.0)), int(std::ceil(cy + extent + 1.0))))
                      & m_bounds;
    if (box.isEmpty())
        return;

    const float opacity = float(std::clamp(m_settings.opacity, 0.0, 1.0));
    const Pixel def = m_before.defaultPixel();
    const float reach = float(extent + 1.0);
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
    // Stretched paper runs along the stroke: the tip's narrow way.
    const float stretch = float(std::clamp(m_settings.grainStretch, 1.0, 40.0));
    const float ax = -wy, ay = wx;

    for (const TileCoord c : TileStore::tilesIntersecting(box)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & box;

        std::vector<float> &mask = m_mask[c];
        if (mask.empty())
            mask.assign(size_t(N) * N, 0.0f);
        float *paint = nullptr;
        if (m_multiColor) {
            std::vector<float> &pv = m_paint[c];
            if (pv.empty())
                pv.assign(size_t(N) * N * 3, 0.0f);
            paint = pv.data();
        }
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
                    float tooth;
                    if (stretch > 1.0f) {
                        // Paper turned to run along the stroke and drawn out that way.
                        const int along = int(std::lround(float(x) * ax + float(y) * ay));
                        const int over = int(std::lround(-float(x) * ay + float(y) * ax));
                        const double gs = std::max(1.0, m_settings.grainSize);
                        tooth = 0.65f * smoothNoise(over, along, gs, gs * stretch, 11)
                                + 0.35f * smoothNoise(over, along, std::max(1.0, gs * 0.5), gs * stretch * 0.5, 12);
                    } else {
                        tooth = paperTooth(x, y, m_settings.grainSize);
                    }
                    const float t = std::clamp((tooth - (toothNeeded - kToothEdge)) / (2.0f * kToothEdge), 0.0f, 1.0f);
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
                // The stroke's colour here: each mark's laid over the last.
                float col[3] = {mark.color[0], mark.color[1], mark.color[2]};
                if (paint) {
                    float *pc = paint + size_t(i) * 3;
                    for (int k = 0; k < 3; ++k) {
                        pc[k] = mark.color[k] * a + pc[k] * (1.0f - a);
                        col[k] = pc[k] / std::max(m, 1e-6f);
                    }
                }

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
                    out[i] = makePixel(col[0] * sa + dr * k, col[1] * sa + dg * k,
                                       col[2] * sa + db * k, sa + da * k);
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
