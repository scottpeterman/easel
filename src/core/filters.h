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
    PencilSketch,
    InkSketch,
};
inline constexpr int FilterTypeCount = 7;

// A filter and its settings. One struct holds every kind's; only those the
// kind uses matter.
struct Filter {
    FilterType type = FilterType::GaussianBlur;
    // Gaussian Blur: how far it spreads, in pixels (0.1..250).
    // Sharpen: the width of the edges it picks out (0.1..50).
    // Pencil Sketch: how soft and broad the shading is (1..100).
    // Ink Sketch: the width of the lines (0.3..10).
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
    // Pencil Sketch: how heavy the pencil is, 0.5..4 (1 = lightest useful).
    double darkness = 1.5;
    // Ink Sketch: how hard the edges are pushed, 1..60. Low keeps only the
    // main outlines; high picks up fine texture too.
    double detail = 20.0;
    // Ink Sketch: how much of the picture goes to solid black, 0..1.
    double ink = 0.3;
    // Ink Sketch: 1..100. Low is a soft wash; high is a hard pen line.
    double hardness = 20.0;

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
//
// Pencil Sketch and Ink Sketch redraw the picture in greys on white, keeping
// the layer's transparency. Both read lightness as the eye sees it (sRGB), with
// transparent areas counted as white paper. Pencil divides each pixel by a
// blurred copy of the picture, which leaves flat areas white and edges shaded.
// Ink is an extended difference of Gaussians: lines along edges, and areas
// darker than the ink level filled solid.
QHash<TileCoord, QImage> applyFilter(TileStore &store, const Selection &clip, const QRect &canvas, const Filter &filter);

} // namespace easeletch
