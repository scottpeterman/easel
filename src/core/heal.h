#pragma once

#include "tilestore.h"

#include <QRect>

#include <vector>

namespace easeletch {

// Spot healing: replaces the marked pixels of a layer with what's around them.
//
// It looks nearby for the patch of the layer whose surroundings best match the
// surroundings of the marked area, copies that patch in (so paper grain or any
// other texture carries over), then shifts its tones so they meet the edge of
// the marked area with no visible seam. With nowhere to copy from (the marked
// area fills the canvas, near enough), it fills smoothly from the edge inward.
//
// area: the rectangle the marks lie in. coverage: one value per pixel of area,
// row by row, 0..1: how much of the pixel is replaced (a soft brush edge fades
// the repair in). Pixels with no coverage are never changed. Returns false if
// there was nothing to do.
bool healArea(TileStore &store, const QRect &canvas, const QRect &area, const std::vector<float> &coverage);

} // namespace easeletch
