#include "colorwheel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTwoPi = 6.283185307179586;

double wrap01(double v)
{
    v = std::fmod(v, 1.0);
    return v < 0.0 ? v + 1.0 : v;
}

} // namespace

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

QColor ColorWheel::color() const
{
    return QColor::fromHsvF(float(m_h), float(m_s), float(m_v));
}

void ColorWheel::setColor(const QColor &color)
{
    const QColor c = color.toHsv();
    if (c.hsvHueF() >= 0.0f) // greys report -1: keep the current hue
        m_h = c.hsvHueF();
    m_s = c.hsvSaturationF();
    m_v = c.valueF();
    update();
}

QPointF ColorWheel::center() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

double ColorWheel::outerRadius() const
{
    return std::min(width(), height()) / 2.0 - 2.0;
}

double ColorWheel::innerRadius() const
{
    const double outer = outerRadius();
    return outer - std::max(12.0, outer * 0.18);
}

QRectF ColorWheel::squareRect() const
{
    const double half = innerRadius() / std::sqrt(2.0) - 4.0;
    return QRectF(center() - QPointF(half, half), QSizeF(2 * half, 2 * half));
}

const QImage &ColorWheel::ringImage(qreal dpr)
{
    const int side = int(std::ceil(std::min(width(), height()) * dpr));
    if (m_ring.width() == side && qFuzzyCompare(m_ring.devicePixelRatio(), dpr))
        return m_ring;

    m_ring = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
    m_ring.fill(Qt::transparent);
    const double c = side / 2.0;
    const double outer = outerRadius() * dpr;
    const double inner = innerRadius() * dpr;
    for (int y = 0; y < side; ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_ring.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const double dx = x + 0.5 - c, dy = y + 0.5 - c;
            const double r = std::hypot(dx, dy);
            // Antialiased by one device pixel on both edges.
            const double cover = std::clamp(std::min(outer - r + 0.5, r - inner + 0.5), 0.0, 1.0);
            if (cover <= 0.0)
                continue;
            // Hue 0 (red) at the right, increasing counter-clockwise.
            const double hue = wrap01(std::atan2(-dy, dx) / kTwoPi);
            QColor col = QColor::fromHsvF(float(hue), 1.0f, 1.0f);
            col.setAlphaF(float(cover));
            line[x] = qPremultiply(col.rgba());
        }
    }
    m_ring.setDevicePixelRatio(dpr);
    return m_ring;
}

const QImage &ColorWheel::squareImage(qreal dpr)
{
    const QRectF sq = squareRect();
    const int n = std::max(1, int(std::ceil(sq.width() * dpr)));
    if (m_square.width() == n && m_squareHue == m_h && qFuzzyCompare(m_square.devicePixelRatio(), dpr))
        return m_square;

    m_square = QImage(n, n, QImage::Format_RGB32);
    for (int y = 0; y < n; ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_square.scanLine(y));
        const double v = 1.0 - (y + 0.5) / n;
        for (int x = 0; x < n; ++x) {
            const double s = (x + 0.5) / n;
            line[x] = QColor::fromHsvF(float(m_h), float(s), float(v)).rgb();
        }
    }
    m_square.setDevicePixelRatio(dpr);
    m_squareHue = m_h;
    return m_square;
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    const QPointF c = center();
    const double outer = outerRadius();
    const double side = std::min(width(), height());

    p.drawImage(QPointF(c.x() - side / 2.0, c.y() - side / 2.0), ringImage(dpr));
    const QRectF sq = squareRect();
    p.drawImage(sq.topLeft(), squareImage(dpr));

    // Hue marker across the ring.
    const double mid = (outer + innerRadius()) / 2.0;
    const double angle = m_h * kTwoPi;
    const QPointF hp = c + QPointF(std::cos(angle), -std::sin(angle)) * mid;
    const double ringHalf = (outer - innerRadius()) / 2.0;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 2.5));
    p.drawEllipse(hp, ringHalf - 1.0, ringHalf - 1.0);
    p.setPen(QPen(QColor(30, 30, 30), 1.0));
    p.drawEllipse(hp, ringHalf + 0.5, ringHalf + 0.5);

    // Saturation/value marker: light ring on dark colours, dark on light.
    const QPointF sp(sq.left() + m_s * sq.width(), sq.top() + (1.0 - m_v) * sq.height());
    const bool light = m_v > 0.6 && m_s < 0.5;
    p.setPen(QPen(light ? QColor(30, 30, 30) : Qt::white, 2.0));
    p.drawEllipse(sp, 6.0, 6.0);
}

void ColorWheel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF pos = event->position();
    const double r = std::hypot(pos.x() - center().x(), pos.y() - center().y());
    if (squareRect().adjusted(-4, -4, 4, 4).contains(pos))
        m_drag = Drag::Square;
    else if (r >= innerRadius() - 2.0 && r <= outerRadius() + 2.0)
        m_drag = Drag::Ring;
    else
        return;
    dragTo(pos);
}

void ColorWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag != Drag::None)
        dragTo(event->position());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = Drag::None;
}

void ColorWheel::dragTo(const QPointF &pos)
{
    if (m_drag == Drag::Ring) {
        const QPointF d = pos - center();
        m_h = wrap01(std::atan2(-d.y(), d.x()) / kTwoPi);
    } else if (m_drag == Drag::Square) {
        const QRectF sq = squareRect();
        m_s = std::clamp((pos.x() - sq.left()) / sq.width(), 0.0, 1.0);
        m_v = std::clamp(1.0 - (pos.y() - sq.top()) / sq.height(), 0.0, 1.0);
    }
    update();
    emit colorChanged(color());
}
