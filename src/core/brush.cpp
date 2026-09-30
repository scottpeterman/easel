#include "brush.h"

#include <algorithm>
#include <cmath>

namespace easel {

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
    return std::max(0.5, std::clamp(spacing, 0.01, 2.0) * diameterAt(pressure));
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
                        const QColor &color, BrushMode mode, const StrokeSample &first)
{
    m_target = target;
    m_before = target->snapshot();
    m_bounds = bounds;
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

    for (auto it = m_mask.cbegin(); it != m_mask.cend(); ++it)
        before.insert(it.key(), m_before.tile(it.key()));

    m_target = nullptr;
    m_before = TileStore();
    m_mask.clear();
    return before;
}

void BrushStroke::paintDabs(const QList<StrokeSample> &dabs)
{
    for (const StrokeSample &d : dabs)
        paintDab(d);
}

void BrushStroke::paintDab(const StrokeSample &dab)
{
    constexpr int N = TileStore::TileSize;
    const double radius = std::max(0.5, m_settings.diameterAt(dab.pressure) * 0.5);
    const float strength = float(m_settings.dabStrengthAt(dab.pressure));
    if (strength <= 0.0f)
        return;
    ++m_dabCount;

    const double cx = dab.pos.x(), cy = dab.pos.y();
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
                const float a = dabCoverage(dist, radius, m_settings.hardness) * strength;
                if (a <= 0.0f)
                    continue;

                const int i = row + (x - tr.left());
                float &m = mask[size_t(i)];
                m += a * (1.0f - m);
                const float cov = m * opacity;

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

} // namespace easel
