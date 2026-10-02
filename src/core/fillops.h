#pragma once

#include "selection.h"
#include "tilestore.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRect>

namespace easeletch {

// Lays a colour over the pixels of region (by its coverage, so a feathered
// region fades), inside clip when that isn't empty, within the canvas.
// Returns the pre-change content of every tile it changed, for History.
QHash<TileCoord, QImage> fillRegion(TileStore &store, const Selection &region, const Selection &clip,
                                    const QRect &canvas, const QColor &color);

// A gradient from one point to another. Linear: start colour at `from` and
// before it, end colour at `to` and beyond. Radial: start colour at `from`,
// end colour at the distance of `to` and beyond. Either colour may be
// transparent (a transparent end keeps the other end's colour and only fades).
struct Gradient {
    QPointF from;
    QPointF to;
    QColor start = Qt::black;
    QColor end = Qt::transparent;
    bool radial = false;
};

// The gradient's colour at t (0 = start, 1 = end): colours are mixed as sRGB
// values, as other editors do, and transparency evenly.
Pixel gradientPixel(const Gradient &gradient, double t);

// Lays the gradient over the selection (by its coverage), or over the whole
// canvas when clip is empty. Returns the pre-change tiles, for History.
QHash<TileCoord, QImage> fillGradient(TileStore &store, const Selection &clip, const QRect &canvas,
                                      const Gradient &gradient);

} // namespace easeletch
