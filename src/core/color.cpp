#include "color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace easel {

namespace {

constexpr int kEncodeLutSize = 4096;

const std::array<float, 256> &decodeLut()
{
    static const auto lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[i] = srgbToLinear(float(i) / 255.0f);
        return t;
    }();
    return lut;
}

const std::array<quint8, kEncodeLutSize> &encodeLut()
{
    static const auto lut = [] {
        std::array<quint8, kEncodeLutSize> t{};
        for (int i = 0; i < kEncodeLutSize; ++i) {
            const float v = linearToSrgb(float(i) / float(kEncodeLutSize - 1));
            t[i] = quint8(std::lround(v * 255.0f));
        }
        return t;
    }();
    return lut;
}

} // namespace

float srgbToLinear(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float srgb8ToLinear(quint8 c)
{
    return decodeLut()[c];
}

quint8 linearToSrgb8(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return encodeLut()[std::size_t(std::lround(c * float(kEncodeLutSize - 1)))];
}

Pixel makePixel(float r, float g, float b, float a)
{
    Pixel p;
    p.r = qfloat16(r);
    p.g = qfloat16(g);
    p.b = qfloat16(b);
    p.a = qfloat16(a);
    return p;
}

bool samePixel(const Pixel &a, const Pixel &b)
{
    return std::memcmp(&a, &b, sizeof(Pixel)) == 0;
}

Pixel pixelFromColor(const QColor &srgb)
{
    const QColor c = srgb.toRgb();
    const float a = float(c.alphaF());
    return makePixel(srgbToLinear(float(c.redF())) * a,
                     srgbToLinear(float(c.greenF())) * a,
                     srgbToLinear(float(c.blueF())) * a,
                     a);
}

QColor pixelToColor(const Pixel &p)
{
    const float a = std::clamp(float(p.a), 0.0f, 1.0f);
    if (a <= 0.0f)
        return QColor(0, 0, 0, 0);
    QColor c;
    c.setRgbF(linearToSrgb(float(p.r) / a), linearToSrgb(float(p.g) / a),
              linearToSrgb(float(p.b) / a), a);
    return c;
}

QRgb pixelToDisplay(const Pixel &p)
{
    const float a = std::clamp(float(p.a), 0.0f, 1.0f);
    if (a <= 0.0f)
        return 0;
    const int a8 = int(std::lround(a * 255.0f));
    const auto premul = [a8](quint8 v) { return (int(v) * a8 + 127) / 255; };
    return qRgba(premul(linearToSrgb8(float(p.r) / a)),
                 premul(linearToSrgb8(float(p.g) / a)),
                 premul(linearToSrgb8(float(p.b) / a)),
                 a8);
}

} // namespace easel
