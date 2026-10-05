#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace easeletch {

// A shape drawn point by point: a polygon, or an open line through the
// points, with an outline and a fill. The points are in canvas pixels.
struct ShapeSettings {
    QList<QPointF> points;
    bool closed = true;  // the last point joins back to the first
    bool curved = false; // a smooth curve through the points, not straight sides
    int line = 3;        // outline width in canvas pixels; 0 = none
    QColor lineColor = Qt::black;
    bool filled = false; // closed shapes only
    QColor fill = Qt::white;
    // Off draws hard edges with no in-between pixels, for sprites and pixel art.
    bool smooth = true;

    static constexpr int MaxLine = 200;
    static constexpr int MaxPoints = 2000;

    // Enough points to draw something.
    bool isDrawable() const { return points.size() >= 2; }
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
