#include "brush.h"

#include "heal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

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

// Smooth noise at a point given in fractions of a pixel, with bumps scaleX
// by scaleY across. For patterns laid along a stroke's path, whose
// coordinates fall between pixels: rounding them to whole pixels would show
// as a fine grid.
float noiseAt(float x, float y, float scaleX, float scaleY, int salt)
{
    const float fx = x / std::max(scaleX, 1.0f), fy = y / std::max(scaleY, 1.0f);
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
    m_granules.clear();
    m_watercolor = mode == BrushMode::Paint && !settings.pixel && settings.smear <= 0.0 && settings.watercolor > 0.0;
    m_multiColor = mode == BrushMode::Paint && !settings.pixel && settings.smear <= 0.0
                   && (settings.valueJitter > 0.0 || settings.hueJitter > 0.0 || settings.edge > 0.0
                       || settings.relief > 0.0 || settings.plates > 0.0);
    if (m_multiColor)
        m_paint.reserve(8192);
    m_dabCount = 0;
    m_jitterStep = 0;
    m_hasGrainAxis = false;
    m_grainOrigin = first.pos;
    const bool shaped = settings.tip != BrushTip::Round && !settings.pixel;
    m_needsHeading = !settings.pixel
                     && ((shaped && settings.followStroke) || settings.streaks > 0.0
                         || (settings.edge > 0.0 && mode == BrushMode::Paint)
                         || (settings.grain > 0.0 && (settings.grainStretch > 1.0 || settings.rollGrain))
                         || settings.roughness > 0.0
                         || (settings.relief > 0.0 && mode == BrushMode::Paint)
                         || (settings.plates > 0.0 && settings.rollGrain && mode == BrushMode::Paint));
    m_hasHeading = false;
    m_hasPending = false;
    m_pendingAlong = 0.0;
    m_along = 0.0;
    m_hasLastDab = false;
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
        paintOne(m_pending, QPointF(std::cos(a), std::sin(a)), m_pendingAlong);
    }
    if (m_mode == BrushMode::Heal)
        finishHeal();
    if (m_watercolor)
        finishWatercolor();

    for (const TileCoord c : std::as_const(m_touched))
        before.insert(c, m_before.tile(c));

    m_target = nullptr;
    m_before = TileStore();
    m_mask.clear();
    m_paint.clear();
    m_granules.clear();
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
        // How far along its path the stroke is here, measured on the line
        // before any jitter knocks the dab off it.
        if (m_hasLastDab)
            m_along += std::hypot(d.pos.x() - m_lastDab.x(), d.pos.y() - m_lastDab.y());
        m_lastDab = d.pos;
        m_hasLastDab = true;
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
                m_pendingAlong = m_along;
                m_hasPending = true;
                continue;
            }
        }
        // The tip's wide side: across the stroke if it turns with it.
        const QPointF across(-m_heading.y(), m_heading.x());
        const QPointF wide = turns ? across : (m_needsHeading && m_settings.tip == BrushTip::Round) ? across : held;
        if (m_hasPending) {
            m_hasPending = false;
            paintOne(m_pending, wide, m_pendingAlong);
        }
        double offset = 0.0;
        if (jitter > 0.0) {
            // Off the line by a repeatable amount: the same stroke scatters the
            // same way, and its mirror images scatter with it.
            const quint32 h = hash2(int(m_jitterStep), 0x51ED);
            ++m_jitterStep;
            const double angle = unit(h) * 6.283185307179586;
            const double reach = unit(hash2(int(h), 7)) * jitter * m_settings.diameterAt(d.pressure);
            const QPointF shift = QPointF(std::cos(angle), std::sin(angle)) * reach;
            d.pos += shift;
            const QPointF head = headingFor(wide);
            offset = shift.x() * -head.y() + shift.y() * head.x();
        }
        paintOne(d, wide, m_along, offset);
    }
}

QPointF BrushStroke::headingFor(const QPointF &wide) const
{
    if (m_hasHeading)
        return m_heading;
    // A tip turned with the stroke has its wide side square to the way it's
    // going; one held still says nothing, so take the turn from it anyway.
    return QPointF(wide.y(), -wide.x());
}

float BrushStroke::tear(double along, int side, double halfWidth) const
{
    const double rough = std::clamp(m_settings.roughness, 0.0, 1.0);
    if (rough <= 0.0 || m_settings.pixel)
        return 0.0f;
    // Long runs along the stroke with shorter splits in them: the stringy,
    // torn look of a bark plate's side. Each side, and each stroke, its own.
    // The pattern's scale is the stroke's, not the dab's: dabs pressed
    // harder or lighter must still agree on where the edge is.
    const double hw = std::max(1.0, halfWidth);
    const double scale = std::max(1.0, m_settings.size * 0.5);
    const int salt = int(m_strokeSeed & 0x3FF) * 2 + (side > 0 ? 1 : 0) + 40;
    const float at = float(along);
    const float broad = noiseAt(at, 0.5f, float(std::max(2.0, scale * 1.6)), 1.0f, salt);
    const float fine = noiseAt(at, 0.5f, float(std::max(1.5, scale * 0.3)), 1.0f, salt + 5000);
    float g = (0.7f * broad + 0.3f * fine) * 2.0f - 1.0f; // -1..1
    // Splits bite in deeper than the edge swells out.
    if (g < 0.0f)
        g = -std::pow(-g, 0.7f) * 1.25f;
    return float(rough * hw * 0.5) * g;
}

float BrushStroke::plateShade(float px, float py, float lx, float ly) const
{
    const float cs = float(std::clamp(m_settings.plateSize, 3.0, 400.0));
    const float st = float(std::clamp(m_settings.plateStretch, 1.0, 12.0));
    const int salt = 61;
    // Wander the net a little, so no fissure runs dead straight.
    const float wx = (noiseAt(px, py, cs * 0.7f, cs * st * 0.5f, salt) - 0.5f) * cs * 0.55f;
    const float wy = (noiseAt(px, py, cs * 0.9f, cs * st * 0.9f, salt + 1) - 0.5f) * cs * st * 0.3f;
    // In plate units: one plate is about one unit each way.
    const float qx = (px + wx) / cs, qy = (py + wy) / (cs * st);
    const int ix = int(std::floor(qx)), iy = int(std::floor(qy));
    // The nearest two plate centres: the fissure runs where they're equally near.
    float f1 = 1e9f, f2 = 1e9f;
    float c1x = 0.0f, c1y = 0.0f, c2x = 0.0f, c2y = 0.0f;
    quint32 c1h = 0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            const quint32 h = hash2(ix + i + salt * 7919, iy + j);
            // Rows of plates offset like brickwork, which is how a fissure
            // net interlaces; then each centre wanders within its cell.
            const float cx = float(ix + i) + 0.5f + (unit(h) - 0.5f) * 0.7f;
            const float cy = float(iy + j) + 0.5f * float((ix + i) & 1) + (unit(hash2(int(h), 3)) - 0.5f) * 0.6f;
            const float ddx = qx - cx, ddy = (qy - cy) * 1.0f;
            const float dd = ddx * ddx + ddy * ddy;
            if (dd < f1) {
                f2 = f1;
                c2x = c1x;
                c2y = c1y;
                f1 = dd;
                c1x = ddx;
                c1y = ddy;
                c1h = h;
            } else if (dd < f2) {
                f2 = dd;
                c2x = ddx;
                c2y = ddy;
            }
        }
    const float d1 = std::sqrt(f1), d2 = std::sqrt(f2);
    // How far from the fissure, in pixels across the plate.
    const float edge = (d2 - d1) * 0.5f * cs;
    // Fissures open and close as they run, and their sides are ragged.
    const float width = std::max(0.8f, cs * 0.13f)
                        * (0.45f + 1.1f * noiseAt(px, py, cs * 0.5f, cs * st * 0.35f, salt + 2))
                        * (0.8f + 0.4f * noiseAt(px, py, 2.0f, 3.0f, salt + 3));
    const float c = std::clamp((width - edge) / std::max(0.8f, width * 0.6f) + 0.5f, 0.0f, 1.0f);
    float crack = c * c * (3.0f - 2.0f * c);
    // Bark splits along the trunk as it grows. A fissure across it, between
    // one plate and the next one up, is shallow and often not there at all.
    const float nx = c2x - c1x, ny = (c2y - c1y) * st; // across the fissure, in true proportion
    const float nl = std::sqrt(nx * nx + ny * ny);
    if (nl > 1e-5f) {
        const float acrossTrunk = std::abs(nx) / nl; // 1 for a fissure running along it
        const float keep = noiseAt(px, py, cs * 1.5f, cs * st * 0.6f, salt + 5) > 0.55f ? 0.55f : 0.12f;
        crack *= keep + (1.0f - keep) * acrossTrunk * acrossTrunk;
    }

    // Each plate is a little domed: lit on the side towards the light, and
    // falling into shadow on the far side, nearest the fissure.
    const float len = std::sqrt(c1x * c1x + c1y * c1y);
    float shade = 0.0f;
    if (len > 1e-4f) {
        const float rim = std::clamp(1.0f - edge / (cs * 0.45f), 0.0f, 1.0f); // 1 at the fissure
        shade = ((c1x * lx + c1y * ly) / len) * rim * rim * 0.4f;
        // And both sides darken going down into the fissure.
        shade -= rim * rim * rim * 0.45f;
    }
    // Plates differ: some lighter, some darker.
    shade += (unit(hash2(int(c1h), 9)) - 0.5f) * 0.3f;
    // Their faces are rough and stringy, with paler blotches like lichen.
    shade += (noiseAt(px, py, std::max(1.0f, cs * 0.1f), cs * st * 0.25f, salt + 4) - 0.5f) * 0.3f;
    shade += std::max(0.0f, noiseAt(px, py, cs * 0.6f, cs * 0.8f, salt + 6) - 0.62f) * 0.9f;
    return shade - crack * 1.6f;
}

void BrushStroke::paintOne(const StrokeSample &d, const QPointF &wide, double along, double offset)
{
    if (smears()) {
        smudgeDab(d, wide);
        return;
    }
    QList<Mark> marks;
    marksFor(d, wide, along, offset, marks);
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
        // A mirror image turns left into right: its offset off the middle of
        // its stroke changes side. Turned both ways, it's only rotated.
        if (acrossX) {
            mm.pos = QPointF(flipped.x(), c.y());
            mm.wide = QPointF(-m.wide.x(), m.wide.y());
            mm.heading = QPointF(-m.heading.x(), m.heading.y());
            mm.offset = -m.offset;
            paintMark(mm);
        }
        if (acrossY) {
            mm.pos = QPointF(c.x(), flipped.y());
            mm.wide = QPointF(m.wide.x(), -m.wide.y());
            mm.heading = QPointF(m.heading.x(), -m.heading.y());
            mm.offset = -m.offset;
            paintMark(mm);
        }
        if (acrossX && acrossY) {
            mm.pos = flipped;
            mm.wide = QPointF(-m.wide.x(), -m.wide.y());
            mm.heading = QPointF(-m.heading.x(), -m.heading.y());
            mm.offset = m.offset;
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

void BrushStroke::marksFor(const StrokeSample &d, const QPointF &wide, double along, double offset,
                           QList<Mark> &out)
{
    const int first = int(out.size());
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

    // Where each mark sits on the stroke.
    const QPointF head = headingFor(wide);
    const QPointF left(-head.y(), head.x());
    for (int i = first; i < int(out.size()); ++i) {
        Mark &m = out[i];
        const QPointF rel = m.pos - d.pos;
        m.heading = head;
        m.along = along + rel.x() * head.x() + rel.y() * head.y();
        m.offset = offset + rel.x() * left.x() + rel.y() * left.y();
    }
}

float BrushStroke::tipCoverage(float dx, float dy, double radius, const QPointF &wide, float grow) const
{
    if (m_settings.pixel)
        return coverageAt(std::sqrt(dx * dx + dy * dy), radius);
    if (m_settings.tip == BrushTip::Round)
        return coverageAt(std::max(0.0f, std::sqrt(dx * dx + dy * dy) - grow), radius);

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
    // A torn edge moves the soft falloff out or in with it.
    outside -= grow;
    depth -= grow / a;
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
    // A torn edge can swell out past it by half its roughness.
    const double rough = m_settings.pixel ? 0.0 : std::clamp(m_settings.roughness, 0.0, 1.0);
    const double extent = radius * (m_settings.tip == BrushTip::Fan && !m_settings.pixel ? 1.8 : 1.0 + 0.5 * rough);
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
    if (stretch > 1.0f && !m_hasGrainAxis) {
        m_grainAxis = QPointF(-wy, wx); // along the stroke as it set off
        m_hasGrainAxis = true;
    }
    const float ax = float(m_grainAxis.x()), ay = float(m_grainAxis.y());
    const float ox = float(m_grainOrigin.x()), oy = float(m_grainOrigin.y());

    // Where a pixel is on the stroke's own path: along it and off its middle.
    const bool rolling = !m_settings.pixel && m_settings.rollGrain && grain > 0.0f;
    const float relief = (m_settings.pixel || m_mode != BrushMode::Paint)
                             ? 0.0f
                             : float(std::clamp(m_settings.relief, 0.0, 1.0));
    const float plates = (m_settings.pixel || m_mode != BrushMode::Paint)
                             ? 0.0f
                             : float(std::clamp(m_settings.plates, 0.0, 1.0));
    const bool platesRoll = plates > 0.0f && m_settings.rollGrain;
    const bool onPath = rolling || rough > 0.0 || relief > 0.0f || platesRoll;
    const float hx = float(mark.heading.x()), hy = float(mark.heading.y());
    const float mAlong = float(mark.along), mOffset = float(mark.offset);
    // The stroke's half-width here. The torn edge is keyed to the distance
    // along the path, not to the dab, so overlapping dabs agree on it.
    const float hw = float(std::max(1.0, radius));
    // Relief: how squarely the light falls across the stroke (+ lights its
    // left side).
    float lightAcross = 0.0f, lightAlong = 0.0f, lightX = 0.0f, lightY = 0.0f;
    if (relief > 0.0f || plates > 0.0f) {
        const double la = m_settings.lightAngle * 3.141592653589793 / 180.0;
        lightX = float(std::cos(la));
        lightY = float(std::sin(la));
        lightAcross = float(std::cos(la) * -mark.heading.y() + std::sin(la) * mark.heading.x());
        lightAlong = float(std::cos(la) * mark.heading.x() + std::sin(la) * mark.heading.y());
    }

    for (const TileCoord c : TileStore::tilesIntersecting(box)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & box;

        std::vector<float> &mask = m_mask[c];
        if (mask.empty())
            mask.assign(size_t(N) * N, 0.0f);
        const float *gran = m_watercolor ? granules(c) : nullptr;
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
                float sAlong = 0.0f, sOver = 0.0f;
                if (onPath) {
                    sAlong = mAlong + dx * hx + dy * hy;
                    sOver = mOffset + (-dx * hy + dy * hx);
                }
                const float grow = rough > 0.0 ? tear(sAlong, sOver >= 0.0f ? 1 : -1, hw) : 0.0f;
                float a = tipCoverage(dx, dy, radius, wide, grow) * strength;
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
                    if (rolling) {
                        // Paper laid along the stroke's path: its streaks
                        // bend round with it, square to its sides.
                        const float gs = float(std::max(1.0, m_settings.grainSize));
                        const int salt = 21 + int(m_strokeSeed & 0xFF) * 2;
                        tooth = 0.65f * noiseAt(sOver, sAlong, gs, gs * stretch, salt)
                                + 0.35f * noiseAt(sOver, sAlong, std::max(1.0f, gs * 0.5f), gs * stretch * 0.5f, salt + 1);
                    } else if (stretch > 1.0f) {
                        // Paper turned to run along the stroke and drawn out that way.
                        const float rx = float(x) - ox, ry = float(y) - oy;
                        const int along = int(std::lround(rx * ax + ry * ay));
                        const int over = int(std::lround(-rx * ay + ry * ax));
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
                float mc[3] = {mark.color[0], mark.color[1], mark.color[2]};
                if (relief > 0.0f || plates > 0.0f) {
                    float s = 0.0f;
                    if (plates > 0.0f) {
                        // The net across and along: the stroke's path, or the
                        // canvas running up and down. The canvas's "along"
                        // is downwards, its "across" to the right; the
                        // stroke's across is to its left.
                        s += plates * (platesRoll ? plateShade(sOver, sAlong, lightAcross, lightAlong)
                                                  : plateShade(float(x) + 0.5f, float(y) + 0.5f, lightX, lightY));
                    }
                    if (relief > 0.0f) {
                        // Across the ridge, -1 one side to 1 the other, and how
                        // much light that part of its round face catches.
                        const float t = std::clamp(sOver / hw, -1.0f, 1.0f);
                        s += relief * 1.2f * lightAcross * (0.75f * t + 0.25f * t * std::abs(t));
                        // Its sides fall away into the cracks between ridges.
                        const float t2 = t * t;
                        s -= relief * 0.7f * t2 * t2;
                        // Fibres: fine light and dark threads running along it,
                        // and here and there a dark split. Sized by the stroke,
                        // not the dab, so every dab lays the same threads.
                        const float fw = float(std::max(1.5, m_settings.size * 0.06));
                        const int fs = 31 + int(m_strokeSeed & 0xFF) * 3;
                        const float fib = 0.65f * noiseAt(sOver, sAlong, fw, fw * 10.0f, fs)
                                          + 0.35f * noiseAt(sOver, sAlong, fw * 0.6f, fw * 5.0f, fs + 1);
                        s += relief * 0.55f * (fib * 2.0f - 1.0f);
                        const float split = noiseAt(sOver, sAlong, fw * 1.6f, fw * 22.0f, fs + 2);
                        const float sk = std::clamp((split - 0.7f) / 0.12f, 0.0f, 1.0f);
                        s -= relief * 1.3f * sk * sk * (3.0f - 2.0f * sk);
                    }
                    s = std::clamp(s, -0.95f, 1.0f);
                    // Linear light: shade by exposure, so the hue holds; a
                    // touch of warm white on the brightest catches.
                    const float gain = s >= 0.0f ? 1.0f + 2.6f * s : 1.0f + s;
                    const float lift = std::max(0.0f, s - 0.55f) * 0.25f;
                    for (int k = 0; k < 3; ++k)
                        mc[k] = std::min(1.0f, mc[k] * gain + lift);
                }
                float col[3] = {mc[0], mc[1], mc[2]};
                if (paint) {
                    float *pc = paint + size_t(i) * 3;
                    for (int k = 0; k < 3; ++k) {
                        pc[k] = mc[k] * a + pc[k] * (1.0f - a);
                        col[k] = pc[k] / std::max(m, 1e-6f);
                    }
                }

                const Pixel &d = before ? before[i] : def;
                const float dr = float(d.r), dg = float(d.g), db = float(d.b), da = float(d.a);
                if (m_mode == BrushMode::Erase) {
                    // Towards what erasing leaves: nothing, or the page's colour.
                    const float k = 1.0f - cov;
                    out[i] = makePixel(dr * k + float(m_eraseTo.r) * cov, dg * k + float(m_eraseTo.g) * cov,
                                       db * k + float(m_eraseTo.b) * cov, da * k + float(m_eraseTo.a) * cov);
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
                } else if (m_watercolor && m_settings.clearWater) {
                    // Water: nothing to see, only the mask, which wets the paper.
                    continue;
                } else if (m_watercolor) {
                    // Wet: an even stain for now; the rim darkens when it dries.
                    out[i] = stain(d, col, std::min(1.0f, m_colorAlpha * cov * (gran ? gran[i] : 1.0f)));
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

namespace {

// Runs work(i) for i in [0, n), spread over the cores when there's enough of it.
template <typename F>
void parallelFor(int n, int minPerWorker, const F &work)
{
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = std::min<unsigned>({cores, 16u, unsigned(std::max(1, n / std::max(1, minPerWorker)))});
    std::atomic<int> next{0};
    const auto run = [&] {
        for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1))
            work(i);
    };
    if (workers <= 1) {
        run();
        return;
    }
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < workers; ++t)
        pool.emplace_back(run);
    run();
    for (std::thread &t : pool)
        t.join();
}

} // namespace

Pixel BrushStroke::stain(const Pixel &d, const float col[3], float k)
{
    // Over paint, it multiplies: each channel keeps (1 - k) of itself plus k
    // of itself times the colour, so it can only darken. Over nothing, it lays
    // down as a wash of that strength. A half-covered pixel gets some of both.
    const float da = float(d.a), empty = 1.0f - da;
    const float r = float(d.r) * (1.0f - k * (1.0f - col[0])) + col[0] * k * empty;
    const float g = float(d.g) * (1.0f - k * (1.0f - col[1])) + col[1] * k * empty;
    const float b = float(d.b) * (1.0f - k * (1.0f - col[2])) + col[2] * k * empty;
    return makePixel(r, g, b, da + k * empty);
}

const float *BrushStroke::granules(TileCoord c)
{
    if (m_settings.granulation <= 0.0)
        return nullptr;
    constexpr int N = TileStore::TileSize;
    std::vector<float> &g = m_granules[c];
    if (g.empty()) {
        g.resize(size_t(N) * N);
        const QRect tr = TileStore::tileRect(c);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                g[size_t(y * N + x)] = granuleAt(tr.left() + x, tr.top() + y);
    }
    return g.data();
}

float BrushStroke::granuleAt(int x, int y) const
{
    const float gran = float(std::clamp(m_settings.granulation, 0.0, 1.0));
    if (gran <= 0.0f)
        return 1.0f;
    // More in the hollows, less on the high points: soft clumps a few bumps
    // of the paper across, with a little finer grit in them.
    const double gs = std::max(1.0, m_settings.grainSize);
    const float tooth = 0.65f * smoothNoise(x, y, gs * 2.2, gs * 2.2, 21) + 0.35f * smoothNoise(x, y, gs, gs, 22);
    return std::max(0.0f, 1.0f + gran * 1.1f * (0.5f - tooth));
}

// --- Wet paper ------------------------------------------------------------------

float WetPaper::left(double age)
{
    // Most of the water stays a while, then the paper dries off quickly.
    const double t = std::clamp(age / DryTime, 0.0, 1.0);
    return float(1.0 - t * t);
}

void WetPaper::sample(const QRect &area, double now, std::vector<float> &out) const
{
    constexpr int N = TileStore::TileSize;
    out.assign(size_t(area.width()) * size_t(area.height()), 0.0f);
    if (m_tiles.isEmpty())
        return;
    for (const TileCoord c : TileStore::tilesIntersecting(area)) {
        const auto it = m_tiles.constFind(c);
        if (it == m_tiles.cend())
            continue;
        const float k = left(now - it->time);
        if (k <= 0.0f)
            continue;
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & area;
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                out[size_t(y - area.top()) * size_t(area.width()) + size_t(x - area.left())] =
                    it->wet[size_t((y - tr.top()) * N + (x - tr.left()))] * k;
    }
}

float WetPaper::wetnessAt(int x, int y, double now) const
{
    constexpr int N = TileStore::TileSize;
    const TileCoord c = TileStore::tileAt(x, y);
    const auto it = m_tiles.constFind(c);
    if (it == m_tiles.cend())
        return 0.0f;
    const QRect tr = TileStore::tileRect(c);
    return it->wet[size_t((y - tr.top()) * N + (x - tr.left()))] * left(now - it->time);
}

void WetPaper::wet(const QRect &area, const std::vector<float> &amount, double now)
{
    constexpr int N = TileStore::TileSize;
    for (const TileCoord c : TileStore::tilesIntersecting(area)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & area;
        bool any = false;
        for (int y = part.top(); y <= part.bottom() && !any; ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                if (amount[size_t(y - area.top()) * size_t(area.width()) + size_t(x - area.left())] > 0.0f) {
                    any = true;
                    break;
                }
        if (!any)
            continue;
        Tile &t = m_tiles[c];
        if (t.wet.empty()) {
            t.wet.assign(size_t(N) * N, 0.0f);
        } else {
            // Bring what's there up to now before adding to it.
            const float k = left(now - t.time);
            for (float &v : t.wet)
                v *= k;
        }
        t.time = now;
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                float &v = t.wet[size_t((y - tr.top()) * N + (x - tr.left()))];
                v = std::max(v, amount[size_t(y - area.top()) * size_t(area.width()) + size_t(x - area.left())]);
            }
    }
}

void WetPaper::prune(double now)
{
    for (auto it = m_tiles.begin(); it != m_tiles.end();)
        it = now - it->time >= DryTime ? m_tiles.erase(it) : std::next(it);
}

namespace {

// Two box blurs each way, radius r: close to a Gaussian. Rows and columns are
// spread over the cores.
void blurField(const std::vector<float> &in, std::vector<float> &out, int w, int h, int r)
{
    out = in;
    if (r < 1)
        return;
    std::vector<float> tmp(in.size());
    const auto rows = [&](const std::vector<float> &src, std::vector<float> &dst) {
        parallelFor(h, 64, [&](int y) {
            const float *row = src.data() + size_t(y) * size_t(w);
            float *o = dst.data() + size_t(y) * size_t(w);
            float sum = 0.0f;
            for (int x = -r; x <= r; ++x)
                sum += row[std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                o[x] = sum / float(2 * r + 1);
                sum += row[std::min(x + r + 1, w - 1)] - row[std::max(x - r, 0)];
            }
        });
    };
    const auto cols = [&](const std::vector<float> &src, std::vector<float> &dst) {
        parallelFor(w, 64, [&](int x) {
            float sum = 0.0f;
            for (int y = -r; y <= r; ++y)
                sum += src[size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(x)];
            for (int y = 0; y < h; ++y) {
                dst[size_t(y) * size_t(w) + size_t(x)] = sum / float(2 * r + 1);
                sum += src[size_t(std::min(y + r + 1, h - 1)) * size_t(w) + size_t(x)]
                       - src[size_t(std::max(y - r, 0)) * size_t(w) + size_t(x)];
            }
        });
    };
    for (int pass = 0; pass < 2; ++pass) {
        rows(out, tmp);
        cols(tmp, out);
    }
}

} // namespace

void BrushStroke::finishWatercolor()
{
    // Drying. Where the paper was dry, the pigment drifts out to the edge of
    // the stroke and dries there darker. Where it was still wet, the paint
    // runs out into the wet paper instead: soft, uneven, feathery, and the
    // further the wetter it was, with only a faint edge where it stopped.
    constexpr int N = TileStore::TileSize;
    QRect strokeArea;
    for (auto it = m_mask.cbegin(); it != m_mask.cend(); ++it)
        strokeArea |= TileStore::tileRect(it.key());
    strokeArea &= m_bounds;
    if (strokeArea.isEmpty())
        return;
    const int reach = m_wet && !m_wet->isEmpty() && !m_settings.clearWater
                          ? int(std::clamp(m_settings.bleed, 0.0, 200.0))
                          : 0;
    const QRect area = strokeArea.adjusted(-2 * reach, -2 * reach, 2 * reach, 2 * reach) & m_bounds;
    const int w = area.width(), h = area.height();
    const size_t cells = size_t(w) * size_t(h);
    const auto at = [&](int x, int y) { return size_t(y - area.top()) * size_t(w) + size_t(x - area.left()); };

    std::vector<float> m(cells, 0.0f);
    for (auto it = m_mask.cbegin(); it != m_mask.cend(); ++it) {
        const QRect tr = TileStore::tileRect(it.key());
        const QRect part = tr & area;
        const std::vector<float> &mask = it.value();
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                m[at(x, y)] = mask[size_t((y - tr.top()) * N + (x - tr.left()))];
    }
    std::vector<float> wetBefore;
    if (m_wet)
        m_wet->sample(area, m_now, wetBefore);
    else
        wetBefore.assign(cells, 0.0f);

    // How much paint ends up where: the stroke itself, and what ran out of it.
    std::vector<float> cover = m;
    bool bled = false;
    if (reach > 0) {
        std::vector<float> nearRun, farRun;
        blurField(m, nearRun, w, h, std::max(2, reach / 4));
        blurField(m, farRun, w, h, std::max(3, reach / 2));
        parallelFor(h, 32, [&](int row) {
            const int y = area.top() + row;
            for (int x = area.left(); x <= area.right(); ++x) {
                const size_t i = at(x, y);
                const float wet = wetBefore[i];
                if (wet <= 0.01f)
                    continue;
                // Some places it runs far, some it hardly moves: that's what
                // gives a bleed its feathery, cauliflower edge.
                const float far = smoothNoise(x, y, reach * 0.7, reach * 0.7, 41);
                const float grit = smoothNoise(x, y, std::max(2.0, reach * 0.18), std::max(2.0, reach * 0.18), 42);
                const float run = nearRun[i] * (1.0f - far) + farRun[i] * far;
                // Uneven only out where it ran: inside the stroke it stays a wash.
                const float uneven = 1.0f + (1.0f - m[i]) * (grit - 0.5f) * 1.4f;
                const float flowed = std::clamp(run * wet * 1.5f * uneven, 0.0f, 1.0f);
                // The stroke's own paint thins as it spreads into the water.
                cover[i] = std::max(m[i] * (1.0f - 0.3f * wet), flowed);
            }
        });
        bled = true;
    }

    // The dried edge: on dry paper a darker rim; on wet paper only a trace,
    // and at the far edge of a bleed, where it stopped, a faint tide line.
    std::vector<float> avg;
    const int r = int(std::clamp(m_settings.size * 0.08, 2.0, 10.0));
    blurField(cover, avg, w, h, r);

    const float strength = float(std::clamp(m_settings.watercolor, 0.0, 1.0));
    const float opacity = float(std::clamp(m_settings.opacity, 0.0, 1.0));
    const Pixel def = m_before.defaultPixel();
    if (!m_settings.clearWater) {
        struct Job {
            TileCoord c;
            const float *mask;
            const float *paint;
            const float *gran;
            QImage before;
            Pixel *out;
        };
        std::vector<Job> jobs;
        for (const TileCoord c : TileStore::tilesIntersecting(area)) {
            const auto mit = m_mask.constFind(c);
            if (mit == m_mask.cend()) {
                if (!bled)
                    continue;
                // Paint may have run into this tile: only touch it if so.
                const QRect part = TileStore::tileRect(c) & area;
                bool any = false;
                for (int y = part.top(); y <= part.bottom() && !any; ++y)
                    for (int x = part.left(); x <= part.right(); ++x)
                        if (cover[at(x, y)] > 0.002f) {
                            any = true;
                            break;
                        }
                if (!any)
                    continue;
            }
            const std::vector<float> *pv = m_multiColor && m_paint.contains(c) ? &m_paint[c] : nullptr;
            m_touched.insert(c);
            jobs.push_back({c, mit == m_mask.cend() ? nullptr : mit.value().data(), pv && !pv->empty() ? pv->data() : nullptr,
                            granules(c), m_before.tile(c), reinterpret_cast<Pixel *>(m_target->writableTile(c).bits())});
        }
        parallelFor(int(jobs.size()), 4, [&](int j) {
            const Job &job = jobs[size_t(j)];
            const QRect tr = TileStore::tileRect(job.c);
            const QRect part = tr & area;
            const auto *before = job.before.isNull() ? nullptr : reinterpret_cast<const Pixel *>(job.before.constBits());
            for (int y = part.top(); y <= part.bottom(); ++y)
                for (int x = part.left(); x <= part.right(); ++x) {
                    const int i = (y - tr.top()) * N + (x - tr.left());
                    const size_t a = at(x, y);
                    const float cv = cover[a];
                    if (cv <= 0.0f)
                        continue;
                    const float clip = m_clip.isEmpty() ? 1.0f : m_clip.coverage(x, y);
                    if (clip <= 0.0f)
                        continue;
                    const float wet = wetBefore[a];
                    // A dried edge is never even: it wanders in and out a little.
                    // (A bleed's edge is uneven already, and its middle mustn't be.)
                    float c = cv;
                    if (cv < 0.98f && wet < 0.5f)
                        c = std::clamp(cv * (1.0f + (1.0f - 2.0f * wet) * (smoothNoise(x, y, 7.0, 7.0, 31) * 0.8f - 0.4f)), 0.0f, 1.0f);
                    float rim = std::clamp((cv - avg[a]) * 2.5f, 0.0f, 1.0f);
                    if (rim > 0.0f)
                        rim *= (0.35f + 0.9f * smoothNoise(x, y, 18.0, 18.0, 32)) * (1.0f - 0.8f * wet);
                    // Where a bleed stopped: a faint line of pigment.
                    float tide = 0.0f;
                    if (wet > 0.05f && cv < 0.35f)
                        tide = wet * std::clamp(1.0f - std::abs(cv - 0.15f) / 0.12f, 0.0f, 1.0f)
                               * smoothNoise(x, y, 9.0, 9.0, 43);
                    const float density = 1.0f - 0.15f * strength + 0.9f * strength * rim + 0.8f * tide;
                    const float k = std::min(1.0f, m_colorAlpha * c * opacity * clip * (job.gran ? job.gran[i] : 1.0f) * density);
                    float col[3] = {m_color[0], m_color[1], m_color[2]};
                    if (job.paint && job.mask && job.mask[i] > 0.0f)
                        for (int ch = 0; ch < 3; ++ch)
                            col[ch] = job.paint[size_t(i) * 3 + size_t(ch)] / std::max(job.mask[i], 1e-6f);
                    job.out[i] = stain(before ? before[i] : def, col, k);
                }
        });
    }

    // What's wet now: where the stroke went, by how wet the brush was.
    if (m_wet) {
        const float water = float(std::clamp(m_settings.clearWater ? std::max(m_settings.wetness, 0.9) : m_settings.wetness, 0.0, 1.0));
        if (water > 0.0f) {
            std::vector<float> amount(cells);
            for (size_t i = 0; i < cells; ++i)
                amount[i] = water * std::clamp(cover[i] * 1.4f, 0.0f, 1.0f);
            m_wet->wet(area, amount, m_now);
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
