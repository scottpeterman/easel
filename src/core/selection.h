#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QPolygonF>
#include <QRect>

namespace easeletch {

// A rectangle, an ellipse, or any set of pixels (a mask, from the magic wand
// or from combining selections). Membership is exact per pixel: a pixel is in
// when its centre is inside the shape, so edges are hard (right for sprites).
// Feathered masks come with M3.
class Selection
{
public:
    enum class Shape { None, Rect, Ellipse, Mask };

    Selection() = default;
    static Selection rect(const QRect &bounds);
    static Selection ellipse(const QRect &bounds);
    // mask: Format_Grayscale8 the size of bounds; non-zero pixels are selected.
    // Trimmed to the pixels actually set.
    static Selection mask(const QRect &bounds, const QImage &mask);

    bool isEmpty() const { return m_shape == Shape::None || m_bounds.isEmpty(); }
    Shape shape() const { return m_shape; }
    QRect bounds() const { return m_bounds; }

    bool contains(int x, int y) const;
    Selection translated(const QPoint &offset) const;
    // Closed outline in canvas coordinates, for drawing (rect and ellipse).
    QPolygonF outline() const;
    // Every edge between selected and unselected pixels, as polylines in
    // canvas coordinates. Mask edges run left-to-right / top-to-bottom.
    QList<QPolygonF> outlines() const;

    // The same pixels as a mask over bounds() (0 / 255).
    QImage maskImage() const;
    Selection united(const Selection &other) const;
    Selection subtracted(const Selection &other) const;
    // Everything in canvas that isn't selected.
    Selection inverted(const QRect &canvas) const;
    // Grown outward (radius > 0) or shrunk inward (radius < 0) by that many
    // pixels, roughly round; growth stays inside clip.
    Selection grown(int radius, const QRect &clip) const;

    friend bool operator==(const Selection &a, const Selection &b);
    friend bool operator!=(const Selection &a, const Selection &b) { return !(a == b); }

private:
    Shape m_shape = Shape::None;
    QRect m_bounds;
    QImage m_mask; // Shape::Mask only
};

} // namespace easeletch
