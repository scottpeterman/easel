#pragma once

#include "fillops.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace easeletch {

// How a curved shape's line goes through one of its points.
struct ShapeNode {
    enum Kind {
        Smooth, // a curve worked out from the points either side
        Corner, // a sharp turn: the curve stops and starts again here
        Handle, // a curve aimed by hand: it leaves towards `out`
    };
    Kind kind = Smooth;
    // Handle: where the curve heads as it leaves the point, relative to it.
    // It arrives from the opposite side, so the line stays smooth through it.
    QPointF out;
    bool operator==(const ShapeNode &) const = default;
};

// A shape drawn point by point: a polygon, or an open line through the
// points, with an outline and a fill. The points are in canvas pixels.
struct ShapeSettings {
    QList<QPointF> points;
    // One for each point, for a curved shape. Any missing are Smooth.
    QList<ShapeNode> nodes;
    bool closed = true;  // the last point joins back to the first
    bool curved = false; // a smooth curve through the points, not straight sides
    int line = 3;        // outline width in canvas pixels; 0 = none
    // An open line thinning to a point at its start and its end, as a pen
    // lifted off the paper does: how much of the line's length each takes,
    // in percent. 0 = the full width right to that end.
    int taperStart = 0;
    int taperEnd = 0;
    QColor lineColor = Qt::black;
    bool filled = false; // closed shapes only
    QColor fill = Qt::white;
    // A gradient for the fill, in place of the one colour. Until its line
    // has been placed by hand it's worked out from the shape and follows it:
    // top to bottom for a linear one, from the middle out for the others.
    bool gradientFill = false;
    GradientStops gradientStops;
    GradientShape gradientShape = GradientShape::Linear;
    bool gradientPlaced = false;
    QPointF gradientFrom;
    QPointF gradientTo;
    bool hasGradient() const { return filled && gradientFill && gradientStops.size() >= 2; }
    // The gradient as it's drawn: its stops and shape, along the line placed
    // or the one worked out.
    Gradient gradient() const;
    // Off draws hard edges with no in-between pixels, for sprites and pixel art.
    bool smooth = true;

    static constexpr int MaxLine = 200;
    static constexpr int MaxPoints = 2000;

    // Enough points to draw something.
    bool isDrawable() const { return points.size() >= 2; }
    // The last point joins the first: it takes three to go round.
    bool isLoop() const { return closed && points.size() >= 3; }
    // An open line with a taper set.
    bool isTapered() const { return !isLoop() && (taperStart > 0 || taperEnd > 0); }
    // The line's width at a place along it, 0 (its start) to 1 (its end).
    double lineWidthAt(double along) const;

    ShapeNode node(int i) const { return i >= 0 && i < nodes.size() ? nodes.at(i) : ShapeNode(); }
    void setNode(int i, const ShapeNode &node);
    // Adds / removes a point, and its node with it.
    void insertPoint(int i, const QPointF &p);
    void removePoint(int i);
    // Where the curve heads leaving point i, relative to it, as it's drawn:
    // worked out for a Smooth point, nothing for a Corner.
    QPointF handleOut(int i) const;
    // The outline as a path: straight sides, or a curve through every point.
    QPainterPath path() const;
    void translate(const QPointF &delta);

    // As a shape layer keeps them in an .easeletch file.
    QJsonObject toJson() const;
    static ShapeSettings fromJson(const QJsonObject &o);
};

// The shape as pixels, in the tile format (RGBA16F, linear light,
// premultiplied), transparent around it; null when there's nothing to draw.
struct ShapeLayout {
    QImage image;
    QPoint origin; // where the image's top-left goes on the canvas
};
ShapeLayout layoutShape(const ShapeSettings &settings);

// Whether a canvas point is on the shape: inside it when it's closed, or
// within tolerance of its line.
bool shapeHit(const ShapeSettings &settings, const QPointF &pos, double tolerance);
// The point within tolerance of pos, nearest first; -1 if none.
int shapePointAt(const ShapeSettings &settings, const QPointF &pos, double tolerance);
// The side within tolerance of pos (straight between the points, also for a
// curved shape): the index of the point it starts at; -1 if none. A new
// point for that side goes in at index + 1.
int shapeSideAt(const ShapeSettings &settings, const QPointF &pos, double tolerance);

} // namespace easeletch
