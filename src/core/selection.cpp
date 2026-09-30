#include "selection.h"

#include <cmath>

namespace easel {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr int kEllipsePoints = 128;
} // namespace

Selection Selection::rect(const QRect &bounds)
{
    Selection s;
    s.m_shape = Shape::Rect;
    s.m_bounds = bounds.normalized();
    return s;
}

Selection Selection::ellipse(const QRect &bounds)
{
    Selection s;
    s.m_shape = Shape::Ellipse;
    s.m_bounds = bounds.normalized();
    return s;
}

bool Selection::contains(int x, int y) const
{
    if (isEmpty() || !m_bounds.contains(x, y))
        return false;
    if (m_shape == Shape::Rect)
        return true;
    // Ellipse inscribed in the bounds; test the pixel centre.
    const double rx = m_bounds.width() / 2.0, ry = m_bounds.height() / 2.0;
    const double cx = m_bounds.left() + rx, cy = m_bounds.top() + ry;
    const double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
    return dx * dx + dy * dy <= 1.0;
}

Selection Selection::translated(const QPoint &offset) const
{
    Selection s = *this;
    s.m_bounds.translate(offset);
    return s;
}

QPolygonF Selection::outline() const
{
    QPolygonF poly;
    if (isEmpty())
        return poly;
    const QRectF r(m_bounds); // pixel edges: x .. x + width
    if (m_shape == Shape::Rect) {
        poly << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft() << r.topLeft();
        return poly;
    }
    const QPointF c = r.center();
    for (int i = 0; i <= kEllipsePoints; ++i) {
        const double a = kTwoPi * i / kEllipsePoints;
        poly << QPointF(c.x() + std::cos(a) * r.width() / 2.0, c.y() + std::sin(a) * r.height() / 2.0);
    }
    return poly;
}

} // namespace easel
