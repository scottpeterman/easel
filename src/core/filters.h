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
    Despeckle,
};
inline constexpr int FilterTypeCount = 5;

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
    // Despeckle: the largest speck removed, in pixels of area (1..5000).
    int speck = 30;
    // Despeckle: how alike neighbouring pixels must be to count as one patch
    // of colour, 0..1 (as the magic wand's tolerance).
    double tolerance = 0.15;

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
//
// Despeckle removes specks: a small patch (up to `speck` pixels) lying wholly
// inside one larger patch of colour takes that patch's colour where they meet.
// Anything bigger is left exactly as it was, so thin lines, dashes and corners
// keep their shape, and so do soft edges, which lie between two patches rather
// than inside one. A speck cut by the edge of the selection is left alone.
QHash<TileCoord, QImage> applyFilter(TileStore &store, const Selection &clip, const QRect &canvas, const Filter &filter);

} // namespace easeletch
