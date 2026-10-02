#pragma once

#include "selection.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QSizeF>
#include <QTransform>

namespace easeletch {

// Free transform of a block of pixels: scaled about its centre (a negative
// scale flips), then rotated (degrees, clockwise), then put with its centre at
// a canvas point.
struct FreeTransform {
    QSizeF size;      // the pixels before the transform
    QPointF center;   // where their centre goes, in canvas coordinates
    double scaleX = 1.0;
    double scaleY = 1.0;
    double angle = 0.0;

    // Maps the source's own pixel coordinates (0,0 .. size) to the canvas.
    QTransform matrix() const;
    // The box's corners on the canvas: top-left, top-right, bottom-right,
    // bottom-left of the source.
    QPolygonF corners() const;
    // Nothing but a move by whole pixels.
    bool isIdentity() const;

    // Mirrors what's on the canvas left-to-right / top-to-bottom, whatever
    // the rotation.
    void flipHorizontal();
    void flipVertical();
    void rotate(double degrees);
    void quarterRight() { rotate(90.0); }
    void quarterLeft() { rotate(-90.0); }
    void halfTurn() { rotate(180.0); }
};

// The parts of the box a drag can start on.
enum class TransformHandle {
    None,
    Move,   // inside the box
    Rotate, // outside it
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
};

// Where each handle sits on the canvas (the eight scaling handles only).
QPointF handlePosition(const FreeTransform &t, TransformHandle handle);
// What a press at pos would grab. tolerance: how near a handle counts, in
// canvas pixels.
TransformHandle hitTest(const FreeTransform &t, const QPointF &pos, double tolerance);

// The transform after dragging a handle from one canvas point to another.
// start: the transform when the drag began.
// constrain (Shift): corners stop keeping proportions, rotation snaps to 15
// degrees, a move keeps to one axis.
// Scaling keeps the opposite handle where it is. While the box isn't rotated,
// sizes come out in whole pixels.
FreeTransform dragHandle(const FreeTransform &start, TransformHandle handle, const QPointF &from,
                         const QPointF &to, bool constrain);

// Resamples an image (tile format: RGBA16F, linear, premultiplied) through a
// transform. origin gets where the result's top-left belongs on the canvas.
// smooth: bilinear, averaged over the source pixels each result pixel covers
// when shrinking; otherwise nearest neighbour, which keeps pixel art hard.
// Flips and quarter turns are exact either way.
// clip: when given, only the part of the result inside it (the canvas) is made.
QImage transformImage(const QImage &source, const QTransform &matrix, bool smooth, QPoint *origin,
                      const QRect &clip = QRect());

// A selection's shape carried through the same transform. shape is relative
// to the source's top-left; the result is relative to the transformed image's.
Selection transformShape(const Selection &shape, const QSize &sourceSize, const QTransform &matrix,
                         const QRect &clip = QRect());

} // namespace easeletch
