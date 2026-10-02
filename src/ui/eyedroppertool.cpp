#include "eyedroppertool.h"

#include <cmath>

EyedropperTool::EyedropperTool(QObject *parent)
    : QObject(parent)
{
}

void EyedropperTool::setDocument(const easeletch::TileStore *store, const QRect &bounds)
{
    m_store = store;
    m_bounds = bounds;
    m_active = false;
}

void EyedropperTool::setCurrentColor(const QColor &color)
{
    if (!m_active) // while picking, the panel follows us; don't chase it
        m_current = color;
}

void EyedropperTool::press(const easeletch::StrokeSample &s)
{
    m_active = true;
    m_previous = m_current;
    m_picked = m_current;
    pick(s.pos);
}

void EyedropperTool::move(const easeletch::StrokeSample &s)
{
    if (m_active)
        pick(s.pos);
}

void EyedropperTool::release(const easeletch::StrokeSample &s)
{
    if (!m_active)
        return;
    pick(s.pos);
    m_active = false;
    m_current = m_picked;
}

bool EyedropperTool::colorPreview(QColor *picked, QColor *previous) const
{
    if (!m_active)
        return false;
    *picked = m_picked;
    *previous = m_previous;
    return true;
}

void EyedropperTool::pick(const QPointF &pos)
{
    if (!m_store)
        return;
    const QPoint p(int(std::floor(pos.x())), int(std::floor(pos.y())));
    if (!m_bounds.contains(p))
        return;
    const QColor raw = easeletch::pixelToColor(m_store->pixel(p.x(), p.y()));
    if (raw.alpha() == 0)
        return; // nothing there to pick
    const QColor c(raw.red(), raw.green(), raw.blue()); // opaque, 8-bit
    if (c == m_picked)
        return;
    m_picked = c;
    emit colorPicked(c);
}
