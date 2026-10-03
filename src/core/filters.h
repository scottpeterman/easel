#pragma once

#include "selection.h"
#include "tilestore.h"

#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>

namespace easeletch {

enum class FilterType {
    GaussianBlur,
    Sharpen,
    Noise,
    Pixelate,
};
inline constexpr int FilterTypeCount = 4;

// A filter and its settings. One struct holds every kind's; only those the
// kind uses matter.
struct Filter {
    FilterType type = FilterType::GaussianBlur;
    // Gaussian Blur: how far it spreads, in pixels (0.1..250).
    // Sharpen: the width of the edges it picks out (0.1..50).
    double radius = 4.0;
    // Sharpen: how strongly, 0..5 (1 = 100%). Noise: how much, 0..1.
    double amount = 1.0;
    // Noise: the same in every channel (grain) rather than coloured specks.
    bool monochrome = false;
    // Noise: a different number gives a different pattern of the same kind.
    quint32 seed = 1;
    // Pixelate: the size of each block, in pixels (2..256).
    int cell = 8;

    static Filter make(FilterType type);
    Filter normalized() const;
};

// Runs a filter over a layer's pixels: inside the selection (by its coverage,
// so a feathered one fades the effect), or over the whole canvas when clip is
// empty. Returns the pre-change content of every tile it changed, for History.
//
// Blur and Sharpen work in linear light on premultiplied colour, so colours
// don't darken where they meet and nothing bleeds out of transparent areas.
// At the edge of the canvas the edge pixels are repeated outward. Noise leaves
// transparency alone and gives the same pattern for the same seed. Pixelate's
// blocks line up with the canvas's top-left corner.
QHash<TileCoord, QImage> applyFilter(TileStore &store, const Selection &clip, const QRect &canvas, const Filter &filter);

} // namespace easeletch
