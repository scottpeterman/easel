#pragma once

#include "selection.h"
#include "tilestore.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QString>

namespace easeletch {

// Lays a colour over the pixels of region (by its coverage, so a feathered
// region fades), inside clip when that isn't empty, within the canvas.
// Returns the pre-change content of every tile it changed, for History.
QHash<TileCoord, QImage> fillRegion(TileStore &store, const Selection &region, const Selection &clip,
                                    const QRect &canvas, const QColor &color);

// How a gradient spreads from the line dragged (from -> to).
enum class GradientShape {
    Linear,    // along the line: first colour at `from` and before it, last at `to` and beyond
    Radial,    // outward from `from`: last colour at the distance of `to` and beyond
    Reflected, // linear, mirrored about `from`: a band, the same both ways (a cylinder)
    Conical,   // swept clockwise round `from`, starting along the line (a disc, a cone seen from above)
};
inline constexpr int GradientShapeCount = 4;
QString gradientShapeKey(GradientShape shape);
GradientShape gradientShapeFromKey(const QString &key);

// One colour along a gradient. position: 0 (the start) to 1 (the end).
struct GradientStop {
    double position = 0.0;
    QColor color;

    friend bool operator==(const GradientStop &a, const GradientStop &b)
    {
        return a.position == b.position && a.color == b.color;
    }
};
using GradientStops = QList<GradientStop>;

// Two stops: one colour to another.
GradientStops twoStops(const QColor &start, const QColor &end);
// In order of position, positions within 0..1, colours valid; never fewer
// than two (a lone stop is doubled, none gives black to white).
GradientStops normalizedStops(GradientStops stops);
// The same stops back to front.
GradientStops reversedStops(const GradientStops &stops);
// Text form for settings: "0:#ffrrggbb;0.5:#ffrrggbb;1:#00rrggbb".
QString stopsToString(const GradientStops &stops);
GradientStops stopsFromString(const QString &text);

// The colour at t along the stops (0..1). Between two stops colours are mixed
// as sRGB values, as other editors do, and transparency evenly; a transparent
// stop has no colour of its own and takes its neighbour's, faded out.
Pixel gradientPixel(const GradientStops &stops, double t);

// A ready-made gradient.
struct GradientPreset {
    QString id;   // stable, for settings
    QString name; // as shown
    GradientStops stops;
};
// Built in: metals (chrome, steel, gold, copper, spun metal for discs) and a
// few skies and spectrums.
const QList<GradientPreset> &gradientPresets();
// Shading for a solid colour: a highlight, the colour, then its shadow. With a
// radial gradient started off-centre it makes a ball; reflected, a rod.
GradientStops shadedStops(const QColor &color);

// A gradient from one point to another. Stops may be transparent.
struct Gradient {
    QPointF from;
    QPointF to;
    GradientStops stops = twoStops(Qt::black, Qt::transparent);
    GradientShape shape = GradientShape::Linear;
};

// Where along the stops (0..1) the centre of pixel (x, y) falls.
double gradientPosition(const Gradient &gradient, int x, int y);

// Lays the gradient over the selection (by its coverage), or over the whole
// canvas when clip is empty. Returns the pre-change tiles, for History.
QHash<TileCoord, QImage> fillGradient(TileStore &store, const Selection &clip, const QRect &canvas,
                                      const Gradient &gradient);

} // namespace easeletch
