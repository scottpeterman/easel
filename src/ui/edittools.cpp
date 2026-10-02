#include "edittools.h"

#include <QGuiApplication>
#include <QKeyEvent>

#include <cmath>

bool TransformTool::keyPress(QKeyEvent *event)
{
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (event->key()) {
    case Qt::Key_Left:
        emit nudged(QPoint(-step, 0));
        return true;
    case Qt::Key_Right:
        emit nudged(QPoint(step, 0));
        return true;
    case Qt::Key_Up:
        emit nudged(QPoint(0, -step));
        return true;
    case Qt::Key_Down:
        emit nudged(QPoint(0, step));
        return true;
    default:
        return false;
    }
}

QPointF GradientTool::endFor(const QPointF &pos) const
{
    if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
        return pos;
    // Shift: horizontal, vertical or diagonal.
    const QPointF d = pos - m_from;
    const double length = std::hypot(d.x(), d.y());
    if (length <= 0.0)
        return pos;
    constexpr double kStep = 3.14159265358979323846 / 4.0;
    const double angle = std::round(std::atan2(d.y(), d.x()) / kStep) * kStep;
    return m_from + QPointF(std::cos(angle), std::sin(angle)) * length;
}

void GradientTool::press(const easeletch::StrokeSample &s)
{
    m_from = s.pos;
    m_active = true;
}

void GradientTool::move(const easeletch::StrokeSample &s)
{
    if (m_active)
        emit dragged(m_from, endFor(s.pos));
}

void GradientTool::release(const easeletch::StrokeSample &s)
{
    if (!m_active)
        return;
    m_active = false;
    emit finished(m_from, endFor(s.pos));
}
