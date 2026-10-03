#include "adjust.h"

#include "srgblut.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace easeletch {

namespace {

constexpr int kTableSize = 1024;

struct TypeKey {
    AdjustmentType type;
    const char *key;
};
constexpr TypeKey kKeys[] = {
    {AdjustmentType::BrightnessContrast, "brightness-contrast"},
    {AdjustmentType::Levels, "levels"},
    {AdjustmentType::Curves, "curves"},
    {AdjustmentType::HueSaturation, "hue-saturation"},
    {AdjustmentType::Exposure, "exposure"},
    {AdjustmentType::BlackWhite, "black-white"},
};

// The tone curve of the adjustments that treat every channel alike.
double tone(const Adjustment &a, double v)
{
    switch (a.type) {
    case AdjustmentType::BrightnessContrast: {
        // Contrast pivots on middle grey; at +1 it's (nearly) a hard threshold.
        const double slope = a.contrast >= 0.0 ? 1.0 / std::max(1.0 - a.contrast, 0.01) : 1.0 + a.contrast;
        return (v - 0.5) * slope + 0.5 + a.brightness;
    }
    case AdjustmentType::Levels: {
        const double span = std::max(a.inWhite - a.inBlack, 1e-4);
        const double t = std::clamp((v - a.inBlack) / span, 0.0, 1.0);
        return a.outBlack + std::pow(t, 1.0 / a.gamma) * (a.outWhite - a.outBlack);
    }
    case AdjustmentType::Curves:
        return curveValue(a.curve, v);
    default:
        return v;
    }
}

bool usesTable(AdjustmentType t)
{
    return t == AdjustmentType::BrightnessContrast || t == AdjustmentType::Levels || t == AdjustmentType::Curves;
}

// HSL <-> RGB, all 0..1 (hue as a fraction of a turn).
void rgbToHsl(const float c[3], float &h, float &s, float &l)
{
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
    l = (mx + mn) * 0.5f;
    const float d = mx - mn;
    if (d <= 1e-6f) {
        h = s = 0.0f;
        return;
    }
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == c[0])
        h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6.0f : 0.0f);
    else if (mx == c[1])
        h = (c[2] - c[0]) / d + 2.0f;
    else
        h = (c[0] - c[1]) / d + 4.0f;
    h /= 6.0f;
}

float hueChannel(float p, float q, float t)
{
    if (t < 0.0f)
        t += 1.0f;
    if (t > 1.0f)
        t -= 1.0f;
    if (t < 1.0f / 6.0f)
        return p + (q - p) * 6.0f * t;
    if (t < 0.5f)
        return q;
    if (t < 2.0f / 3.0f)
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

void hslToRgb(float h, float s, float l, float c[3])
{
    if (s <= 0.0f) {
        c[0] = c[1] = c[2] = l;
        return;
    }
    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    c[0] = hueChannel(p, q, h + 1.0f / 3.0f);
    c[1] = hueChannel(p, q, h);
    c[2] = hueChannel(p, q, h - 1.0f / 3.0f);
}

} // namespace

QString adjustmentKey(AdjustmentType type)
{
    for (const TypeKey &k : kKeys)
        if (k.type == type)
            return QLatin1String(k.key);
    return QString();
}

AdjustmentType adjustmentFromKey(const QString &key)
{
    for (const TypeKey &k : kKeys)
        if (key == QLatin1String(k.key))
            return k.type;
    return AdjustmentType::None;
}

Adjustment Adjustment::normalized() const
{
    Adjustment a = *this;
    const auto unit = [](double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; };
    const auto signedUnit = [](double v) { return std::isfinite(v) ? std::clamp(v, -1.0, 1.0) : 0.0; };
    a.brightness = signedUnit(a.brightness);
    a.contrast = signedUnit(a.contrast);
    a.inBlack = unit(a.inBlack);
    a.inWhite = unit(a.inWhite);
    if (a.inWhite < a.inBlack + 0.004) {
        // The points never cross: at least one 8-bit step apart.
        a.inWhite = std::min(1.0, a.inBlack + 0.004);
        a.inBlack = std::min(a.inBlack, a.inWhite - 0.004);
    }
    a.gamma = std::isfinite(a.gamma) ? std::clamp(a.gamma, 0.1, 10.0) : 1.0;
    a.outBlack = unit(a.outBlack);
    a.outWhite = unit(a.outWhite);
    a.hue = std::isfinite(a.hue) ? std::clamp(a.hue, -180.0, 180.0) : 0.0;
    a.saturation = signedUnit(a.saturation);
    a.lightness = signedUnit(a.lightness);
    a.exposure = std::isfinite(a.exposure) ? std::clamp(a.exposure, -5.0, 5.0) : 0.0;
    for (double *w : {&a.red, &a.green, &a.blue})
        *w = std::isfinite(*w) ? std::clamp(*w, -2.0, 3.0) : 0.0;

    QList<QPointF> pts;
    for (const QPointF &p : a.curve)
        if (std::isfinite(p.x()) && std::isfinite(p.y()))
            pts.append(QPointF(unit(p.x()), unit(p.y())));
    std::stable_sort(pts.begin(), pts.end(), [](const QPointF &p, const QPointF &q) { return p.x() < q.x(); });
    QList<QPointF> distinct;
    for (const QPointF &p : pts)
        if (distinct.isEmpty() || p.x() - distinct.last().x() > 1e-4)
            distinct.append(p);
    if (distinct.size() < 2)
        distinct = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    a.curve = distinct;
    return a;
}

QJsonObject Adjustment::toJson() const
{
    QJsonObject o{{QLatin1String("kind"), adjustmentKey(type)}};
    switch (type) {
    case AdjustmentType::BrightnessContrast:
        o.insert(QLatin1String("brightness"), brightness);
        o.insert(QLatin1String("contrast"), contrast);
        break;
    case AdjustmentType::Levels:
        o.insert(QLatin1String("inBlack"), inBlack);
        o.insert(QLatin1String("inWhite"), inWhite);
        o.insert(QLatin1String("gamma"), gamma);
        o.insert(QLatin1String("outBlack"), outBlack);
        o.insert(QLatin1String("outWhite"), outWhite);
        break;
    case AdjustmentType::Curves: {
        QJsonArray pts;
        for (const QPointF &p : curve)
            pts.append(QJsonArray{p.x(), p.y()});
        o.insert(QLatin1String("points"), pts);
        break;
    }
    case AdjustmentType::HueSaturation:
        o.insert(QLatin1String("hue"), hue);
        o.insert(QLatin1String("saturation"), saturation);
        o.insert(QLatin1String("lightness"), lightness);
        break;
    case AdjustmentType::Exposure:
        o.insert(QLatin1String("exposure"), exposure);
        break;
    case AdjustmentType::BlackWhite:
        o.insert(QLatin1String("red"), red);
        o.insert(QLatin1String("green"), green);
        o.insert(QLatin1String("blue"), blue);
        break;
    case AdjustmentType::None:
        break;
    }
    return o;
}

Adjustment Adjustment::fromJson(const QJsonObject &o)
{
    Adjustment a;
    a.type = adjustmentFromKey(o.value(QLatin1String("kind")).toString());
    const auto num = [&o](const char *key, double fallback) {
        return o.value(QLatin1String(key)).toDouble(fallback);
    };
    a.brightness = num("brightness", a.brightness);
    a.contrast = num("contrast", a.contrast);
    a.inBlack = num("inBlack", a.inBlack);
    a.inWhite = num("inWhite", a.inWhite);
    a.gamma = num("gamma", a.gamma);
    a.outBlack = num("outBlack", a.outBlack);
    a.outWhite = num("outWhite", a.outWhite);
    a.hue = num("hue", a.hue);
    a.saturation = num("saturation", a.saturation);
    a.lightness = num("lightness", a.lightness);
    a.exposure = num("exposure", a.exposure);
    a.red = num("red", a.red);
    a.green = num("green", a.green);
    a.blue = num("blue", a.blue);
    if (const QJsonValue pts = o.value(QLatin1String("points")); pts.isArray()) {
        a.curve.clear();
        for (const QJsonValue &v : pts.toArray()) {
            const QJsonArray p = v.toArray();
            if (p.size() == 2)
                a.curve.append(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
        }
    }
    return a.normalized();
}

bool operator==(const Adjustment &a, const Adjustment &b)
{
    return a.type == b.type && a.brightness == b.brightness && a.contrast == b.contrast && a.inBlack == b.inBlack
           && a.inWhite == b.inWhite && a.gamma == b.gamma && a.outBlack == b.outBlack && a.outWhite == b.outWhite
           && a.curve == b.curve && a.hue == b.hue && a.saturation == b.saturation && a.lightness == b.lightness
           && a.exposure == b.exposure && a.red == b.red && a.green == b.green && a.blue == b.blue;
}

double curveValue(const QList<QPointF> &points, double x)
{
    const qsizetype n = points.size();
    if (n == 0)
        return x;
    if (n == 1 || x <= points.first().x())
        return points.first().y();
    if (x >= points.last().x())
        return points.last().y();

    // Monotone cubic (Fritsch-Carlson): smooth, and between two points it
    // never goes above the higher or below the lower.
    qsizetype i = 0;
    while (i + 2 < n && x > points.at(i + 1).x())
        ++i;
    const auto secant = [&](qsizetype k) {
        const double dx = points.at(k + 1).x() - points.at(k).x();
        return dx > 0.0 ? (points.at(k + 1).y() - points.at(k).y()) / dx : 0.0;
    };
    const auto tangent = [&](qsizetype k) {
        if (k == 0)
            return secant(0);
        if (k == n - 1)
            return secant(n - 2);
        const double a = secant(k - 1), b = secant(k);
        return a * b <= 0.0 ? 0.0 : (a + b) / 2.0;
    };
    const double d = secant(i);
    double m0 = tangent(i), m1 = tangent(i + 1);
    if (d == 0.0) {
        m0 = m1 = 0.0;
    } else {
        const double a = m0 / d, b = m1 / d;
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m0 = t * a * d;
            m1 = t * b * d;
        }
    }
    const double h = points.at(i + 1).x() - points.at(i).x();
    const double t = (x - points.at(i).x()) / h;
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * points.at(i).y() + (t3 - 2 * t2 + t) * h * m0 + (-2 * t3 + 3 * t2) * points.at(i + 1).y()
           + (t3 - t2) * h * m1;
}

AdjustmentKernel::AdjustmentKernel(const Adjustment &adjustment)
    : m_adjust(adjustment.normalized())
{
    if (usesTable(m_adjust.type)) {
        m_table.resize(size_t(kTableSize) + 2);
        for (int i = 0; i < kTableSize + 2; ++i) {
            const double v = std::min(1.0, double(i) / kTableSize);
            m_table[size_t(i)] = float(std::clamp(tone(m_adjust, v), 0.0, 1.0));
        }
    }
    m_gain = float(std::pow(2.0, m_adjust.exposure));
}

void AdjustmentKernel::adjustColor(float c[3]) const
{
    const srgblut::Luts &l = srgblut::luts();
    switch (m_adjust.type) {
    case AdjustmentType::None:
        return;
    case AdjustmentType::Exposure:
        for (int k = 0; k < 3; ++k)
            c[k] = std::clamp(c[k] * m_gain, 0.0f, 1.0f);
        return;
    case AdjustmentType::BrightnessContrast:
    case AdjustmentType::Levels:
    case AdjustmentType::Curves:
        for (int k = 0; k < 3; ++k) {
            const float f = srgblut::encode(l, c[k]) * float(kTableSize);
            const int i = int(f);
            const float v = m_table[size_t(i)] + (m_table[size_t(i) + 1] - m_table[size_t(i)]) * (f - float(i));
            c[k] = srgblut::decode(l, v);
        }
        return;
    case AdjustmentType::HueSaturation: {
        float e[3] = {srgblut::encode(l, c[0]), srgblut::encode(l, c[1]), srgblut::encode(l, c[2])};
        float h, s, lt;
        rgbToHsl(e, h, s, lt);
        h += float(m_adjust.hue / 360.0);
        h -= std::floor(h);
        // Saturation: down to grey at -1, up to twice at +1.
        s = std::clamp(s * float(1.0 + m_adjust.saturation), 0.0f, 1.0f);
        hslToRgb(h, s, lt, e);
        // Lightness: toward white or toward black, keeping the hue.
        const float k = float(m_adjust.lightness);
        for (int j = 0; j < 3; ++j) {
            e[j] = k >= 0.0f ? e[j] + (1.0f - e[j]) * k : e[j] * (1.0f + k);
            c[j] = srgblut::decode(l, e[j]);
        }
        return;
    }
    case AdjustmentType::BlackWhite: {
        const float grey = std::clamp(srgblut::encode(l, c[0]) * float(m_adjust.red)
                                          + srgblut::encode(l, c[1]) * float(m_adjust.green)
                                          + srgblut::encode(l, c[2]) * float(m_adjust.blue),
                                      0.0f, 1.0f);
        c[0] = c[1] = c[2] = srgblut::decode(l, grey);
        return;
    }
    }
}

void AdjustmentKernel::apply(float *px, int count, float amount, const float *strength) const
{
    if (m_adjust.type == AdjustmentType::None || amount <= 0.0f)
        return;
    for (int i = 0; i < count; ++i, px += 4) {
        const float a = px[3];
        if (a <= 0.0f)
            continue;
        const float k = strength ? amount * strength[i] : amount;
        if (k <= 0.0f)
            continue;
        const float inv = 1.0f / a;
        float c[3] = {px[0] * inv, px[1] * inv, px[2] * inv};
        adjustColor(c);
        for (int j = 0; j < 3; ++j)
            px[j] += (c[j] * a - px[j]) * k;
    }
}

} // namespace easeletch
