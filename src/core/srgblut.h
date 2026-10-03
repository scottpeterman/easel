#pragma once

#include "color.h"

#include <algorithm>
#include <array>
#include <cmath>

// sRGB <-> linear for values in 0..1, by table with linear interpolation.
// Shared by the compositor's blend modes and the adjustments.
namespace easeletch::srgblut {

// Encoding is steep near black, so that table is indexed by the square root.
inline constexpr int kLutSize = 4096;

struct Luts {
    std::array<float, kLutSize + 2> encode; // index: sqrt(linear) * kLutSize
    std::array<float, kLutSize + 2> decode; // index: srgb * kLutSize
    Luts()
    {
        for (int i = 0; i < kLutSize + 2; ++i) {
            const float t = std::min(1.0f, float(i) / float(kLutSize));
            encode[size_t(i)] = linearToSrgb(t * t);
            decode[size_t(i)] = srgbToLinear(t);
        }
    }
};

inline const Luts &luts()
{
    static const Luts l;
    return l;
}

inline float lookup(const std::array<float, kLutSize + 2> &table, float t)
{
    const float f = t * float(kLutSize);
    const int i = int(f);
    const float frac = f - float(i);
    return table[size_t(i)] + (table[size_t(i) + 1] - table[size_t(i)]) * frac;
}

inline float encode(const Luts &l, float linear)
{
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return lookup(l.encode, std::sqrt(c));
}

inline float decode(const Luts &l, float srgb)
{
    return lookup(l.decode, std::clamp(srgb, 0.0f, 1.0f));
}

} // namespace easeletch::srgblut
