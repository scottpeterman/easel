#pragma once

#include <QPoint>
#include <QPolygonF>
#include <QRect>

namespace easel {

// A rectangular or elliptical selection. Membership is exact per pixel: a pixel
// is in when its centre is inside the shape, so edges are hard (right for
// sprites). Full raster masks with feathering come with M3.
class Selection
{
public:
    enum class Shape { None, Rect, Ellipse };

    Selection() = default;
    static Selection rect(const QRect &bounds);
    static Selection ellipse(const QRect &bounds);

    bool isEmpty() const { return m_shape == Shape::None || m_bounds.isEmpty(); }
    Shape shape() const { return m_shape; }
    QRect bounds() const { return m_bounds; }

    bool contains(int x, int y) const;
    Selection translated(const QPoint &offset) const;
    // Closed outline in canvas coordinates, for drawing.
    QPolygonF outline() const;

    friend bool operator==(const Selection &a, const Selection &b)
    {
        return a.m_shape == b.m_shape && (a.isEmpty() || a.m_bounds == b.m_bounds);
    }
    friend bool operator!=(const Selection &a, const Selection &b) { return !(a == b); }

private:
    Shape m_shape = Shape::None;
    QRect m_bounds;
};

} // namespace easel
