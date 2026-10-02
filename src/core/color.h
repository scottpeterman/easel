#pragma once

#include <QColor>
#include <QRgb>
#include <QtGui/qrgbafloat.h>

namespace easeletch {

// Internal pixel: linear-light, premultiplied RGBA, 16-bit half-float per channel.
// Memory layout matches QImage::Format_RGBA16FPx4_Premultiplied.
using Pixel = QRgbaFloat16;

float srgbToLinear(float c);
float linearToSrgb(float c);

// Fast paths backed by lookup tables.
float srgb8ToLinear(quint8 c);
quint8 linearToSrgb8(float c);

Pixel makePixel(float r, float g, float b, float a);
bool samePixel(const Pixel &a, const Pixel &b);

// QColor is treated as sRGB-encoded, straight alpha.
Pixel pixelFromColor(const QColor &srgb);
QColor pixelToColor(const Pixel &p);

// Premultiplied 8-bit sRGB, ready for QImage::Format_ARGB32_Premultiplied.
QRgb pixelToDisplay(const Pixel &p);

} // namespace easeletch
