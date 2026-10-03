#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>

#include <vector>

namespace easeletch {

enum class AdjustmentType {
    None,
    BrightnessContrast,
    Levels,
    Curves,
    HueSaturation,
    Exposure,
    BlackWhite,
};
inline constexpr int AdjustmentTypeCount = 7; // with None

// Name used in .easeletch files ("levels", "hue-saturation", ...).
QString adjustmentKey(AdjustmentType type);
AdjustmentType adjustmentFromKey(const QString &key);

// What an adjustment layer does to the picture below it. One struct holds the
// settings of every kind; only those of `type` are used. Every default leaves
// the picture as it is (except Black & White, which is what it's for).
//
// Tones are worked on as sRGB values, as other editors do, so 50% grey is the
// middle of Levels and Curves; Exposure works in linear light, where a stop
// really is twice the light. Transparency is never changed.
struct Adjustment {
    AdjustmentType type = AdjustmentType::None;

    // Brightness / Contrast: each -1..1.
    double brightness = 0.0;
    double contrast = 0.0;

    // Levels: input black and white points and output range, 0..1; gamma
    // 0.1..10 (above 1 lightens the midtones).
    double inBlack = 0.0;
    double inWhite = 1.0;
    double gamma = 1.0;
    double outBlack = 0.0;
    double outWhite = 1.0;

    // Curves: points the curve passes through, input (x) to output (y), 0..1.
    QList<QPointF> curve = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};

    // Hue / Saturation: hue shift in degrees (-180..180); saturation and
    // lightness -1..1.
    double hue = 0.0;
    double saturation = 0.0;
    double lightness = 0.0;

    // Exposure, in stops (-5..5).
    double exposure = 0.0;

    // Black & White: how much each of red, green and blue counts toward the
    // grey (-2..3; the defaults are how bright each looks).
    double red = 0.30;
    double green = 0.59;
    double blue = 0.11;

    static Adjustment make(AdjustmentType type)
    {
        Adjustment a;
        a.type = type;
        return a;
    }

    // Settings in range, curve points in order with distinct inputs.
    Adjustment normalized() const;
    QJsonObject toJson() const;
    static Adjustment fromJson(const QJsonObject &json);

    friend bool operator==(const Adjustment &a, const Adjustment &b);
    friend bool operator!=(const Adjustment &a, const Adjustment &b) { return !(a == b); }
};

// The curve's output for an input (0..1): a smooth line through the points
// that never overshoots between them; flat before the first and after the last.
double curveValue(const QList<QPointF> &points, double x);

// An adjustment made ready to run: tables built once, then applied to any
// number of pixels. Safe to use from several threads at once.
class AdjustmentKernel
{
public:
    explicit AdjustmentKernel(const Adjustment &adjustment);

    // Adjusts count pixels in place: float RGBA, premultiplied, linear light.
    // amount: how much of the adjustment to apply, 0..1 (the layer's opacity).
    // strength: when given, a further 0..1 per pixel (the layer's mask).
    void apply(float *pixels, int count, float amount, const float *strength = nullptr) const;

private:
    void adjustColor(float c[3]) const; // straight, linear light, in and out

    Adjustment m_adjust;
    std::vector<float> m_table; // sRGB in -> sRGB out, for the tone adjustments
    float m_gain = 1.0f;        // exposure
};

} // namespace easeletch
