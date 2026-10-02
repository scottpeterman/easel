#include "selection.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace easeletch {

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

Selection Selection::mask(const QRect &bounds, const QImage &mask)
{
    Selection s;
    if (bounds.isEmpty() || mask.size() != bounds.size())
        return s;
    const QImage m = mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    int x0 = m.width(), y0 = m.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < m.height(); ++y) {
        const uchar *row = m.constScanLine(y);
        for (int x = 0; x < m.width(); ++x) {
            if (!row[x])
                continue;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    if (x1 < 0)
        return s;
    const QRect tight(QPoint(x0, y0), QPoint(x1, y1));
    s.m_shape = Shape::Mask;
    s.m_bounds = tight.translated(bounds.topLeft());
    s.m_mask = tight == QRect(QPoint(0, 0), m.size()) ? m : m.copy(tight);
    return s;
}

Selection Selection::polygon(const QPolygonF &path, const QRect &clip)
{
    const QRect area = path.boundingRect().toAlignedRect() & clip;
    if (path.size() < 3 || area.isEmpty())
        return {};
    QImage m(area.size(), QImage::Format_Grayscale8);
    m.fill(0);
    QPainterPath shape;
    shape.addPolygon(path);
    shape.closeSubpath();
    shape.setFillRule(Qt::WindingFill);
    QPainter p(&m);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.translate(-area.topLeft());
    p.fillPath(shape, Qt::white);
    p.end();
    // The unantialiased fill only ever writes 0 or 255.
    return mask(area, m);
}

float Selection::coverage(int x, int y) const
{
    if (m_shape == Shape::Mask) {
        if (!m_bounds.contains(x, y))
            return 0.0f;
        return float(m_mask.constScanLine(y - m_bounds.top())[x - m_bounds.left()]) / 255.0f;
    }
    return contains(x, y) ? 1.0f : 0.0f;
}

bool Selection::isSoft() const
{
    if (m_shape != Shape::Mask)
        return false;
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *row = m_mask.constScanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            if (row[x] != 0 && row[x] != 255)
                return true;
    }
    return false;
}

bool Selection::contains(int x, int y) const
{
    if (isEmpty() || !m_bounds.contains(x, y))
        return false;
    if (m_shape == Shape::Rect)
        return true;
    if (m_shape == Shape::Mask)
        return m_mask.constScanLine(y - m_bounds.top())[x - m_bounds.left()] != 0;
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
    if (isEmpty() || m_shape == Shape::Mask)
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

QList<QPolygonF> Selection::outlines() const
{
    QList<QPolygonF> lines;
    if (isEmpty())
        return lines;
    if (m_shape != Shape::Mask) {
        lines.append(outline());
        return lines;
    }
    const int w = m_bounds.width(), h = m_bounds.height();
    const int ox = m_bounds.left(), oy = m_bounds.top();
    const auto at = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && m_mask.constScanLine(y)[x] >= 128;
    };
    // Horizontal edges: between rows y-1 and y, merged into runs.
    for (int y = 0; y <= h; ++y) {
        int start = -1;
        for (int x = 0; x <= w; ++x) {
            const bool edge = x < w && at(x, y - 1) != at(x, y);
            if (edge && start < 0)
                start = x;
            if (!edge && start >= 0) {
                lines.append(QPolygonF{QPointF(ox + start, oy + y), QPointF(ox + x, oy + y)});
                start = -1;
            }
        }
    }
    // Vertical edges: between columns x-1 and x.
    for (int x = 0; x <= w; ++x) {
        int start = -1;
        for (int y = 0; y <= h; ++y) {
            const bool edge = y < h && at(x - 1, y) != at(x, y);
            if (edge && start < 0)
                start = y;
            if (!edge && start >= 0) {
                lines.append(QPolygonF{QPointF(ox + x, oy + start), QPointF(ox + x, oy + y)});
                start = -1;
            }
        }
    }
    return lines;
}

QImage Selection::maskImage() const
{
    if (m_shape == Shape::Mask)
        return m_mask;
    QImage m(m_bounds.size(), QImage::Format_Grayscale8);
    m.fill(0);
    if (isEmpty())
        return m;
    for (int y = 0; y < m.height(); ++y) {
        uchar *row = m.scanLine(y);
        for (int x = 0; x < m.width(); ++x)
            row[x] = contains(m_bounds.left() + x, m_bounds.top() + y) ? 255 : 0;
    }
    return m;
}

namespace {

// Combines two selections pixel by pixel over area.
template<typename Op>
Selection combine(const Selection &a, const Selection &b, const QRect &area, Op op)
{
    if (area.isEmpty())
        return {};
    QImage m(area.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < m.height(); ++y) {
        uchar *row = m.scanLine(y);
        for (int x = 0; x < m.width(); ++x)
            row[x] = uchar(std::clamp(int(std::lround(op(a.coverage(area.left() + x, area.top() + y),
                                                         b.coverage(area.left() + x, area.top() + y))
                                                      * 255.0f)),
                                      0, 255));
    }
    return Selection::mask(area, m);
}

} // namespace

Selection Selection::united(const Selection &other) const
{
    if (other.isEmpty())
        return *this;
    if (isEmpty())
        return other;
    return combine(*this, other, m_bounds | other.m_bounds, [](float a, float b) { return std::max(a, b); });
}

Selection Selection::subtracted(const Selection &other) const
{
    if (isEmpty() || other.isEmpty() || !m_bounds.intersects(other.m_bounds))
        return *this;
    return combine(*this, other, m_bounds, [](float a, float b) { return a * (1.0f - b); });
}

Selection Selection::inverted(const QRect &canvas) const
{
    const Selection all = rect(canvas);
    return isEmpty() ? all : all.subtracted(*this);
}

Selection Selection::grown(int radius, const QRect &clip) const
{
    if (isEmpty() || radius == 0)
        return *this;
    const bool grow = radius > 0;
    const int r = std::abs(radius);
    const QRect area = grow ? m_bounds.adjusted(-r, -r, r, r) & clip : m_bounds;
    if (area.isEmpty())
        return {};
    const int w = area.width(), h = area.height();
    std::vector<uchar> cur(size_t(w) * h), next(cur.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            cur[size_t(y) * w + x] = coverage(area.left() + x, area.top() + y) >= 0.5f ? 1 : 0;

    // Alternating 4- and 8-neighbour passes grow an octagon, close to a circle.
    for (int pass = 0; pass < r; ++pass) {
        const bool diagonal = pass % 2 == 1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto at = [&](int dx, int dy) -> uchar {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        return 0; // outside: unselected
                    return cur[size_t(ny) * w + nx];
                };
                uchar v = cur[size_t(y) * w + x];
                const int offsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
                const int n = diagonal ? 8 : 4;
                for (int k = 0; k < n; ++k) {
                    const uchar o = at(offsets[k][0], offsets[k][1]);
                    if (grow)
                        v |= o;
                    else
                        v &= o;
                }
                next[size_t(y) * w + x] = v;
            }
        }
        cur.swap(next);
    }

    QImage m(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *row = m.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = cur[size_t(y) * w + x] ? 255 : 0;
    }
    return mask(area, m);
}

Selection Selection::feathered(int radius, const QRect &clip) const
{
    if (isEmpty() || radius <= 0)
        return *this;
    // Three box blurs come close to a Gaussian. Room for the fade on every
    // side, as far as clip allows; beyond clip counts as unselected.
    const int reach = radius * 2;
    const QRect area = m_bounds.adjusted(-reach, -reach, reach, reach) & clip;
    if (area.isEmpty())
        return {};
    const int w = area.width(), h = area.height();
    std::vector<float> cur(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            cur[size_t(y) * w + x] = coverage(area.left() + x, area.top() + y);

    const int half = std::max(1, int(std::lround(radius * 0.6)));
    const float norm = 1.0f / float(2 * half + 1);
    std::vector<float> line(size_t(std::max(w, h)));
    const auto blurLine = [&](float *data, int count, int stride) {
        for (int i = 0; i < count; ++i)
            line[size_t(i)] = data[i * stride];
        float sum = 0.0f;
        for (int i = 0; i <= half && i < count; ++i)
            sum += line[size_t(i)];
        for (int i = 0; i < count; ++i) {
            data[i * stride] = sum * norm;
            const int out = i - half, in = i + half + 1;
            if (out >= 0)
                sum -= line[size_t(out)];
            if (in < count)
                sum += line[size_t(in)];
        }
    };
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y)
            blurLine(cur.data() + size_t(y) * w, w, 1);
        for (int x = 0; x < w; ++x)
            blurLine(cur.data() + x, h, w);
    }

    QImage m(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *row = m.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = uchar(std::clamp(int(std::lround(cur[size_t(y) * w + x] * 255.0f)), 0, 255));
    }
    return mask(area, m);
}

bool operator==(const Selection &a, const Selection &b)
{
    if (a.isEmpty() || b.isEmpty())
        return a.isEmpty() == b.isEmpty();
    if (a.m_shape != b.m_shape || a.m_bounds != b.m_bounds)
        return false;
    return a.m_shape != Selection::Shape::Mask || a.m_mask == b.m_mask;
}

} // namespace easeletch
