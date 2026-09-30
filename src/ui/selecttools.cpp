#include "selecttools.h"

#include <QGuiApplication>
#include <QKeyEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Selections snap to pixel corners.
QPoint corner(const QPointF &pos)
{
    return QPoint(int(std::lround(pos.x())), int(std::lround(pos.y())));
}

} // namespace

SelectTool::SelectTool(easel::Selection::Shape shape, QObject *parent)
    : QObject(parent)
    , m_shape(shape)
{
}

easel::Selection SelectTool::selectionTo(const QPointF &pos) const
{
    if (!m_cell.isEmpty()) {
        const auto cellOf = [this](const QPointF &p) {
            return QPoint(int(std::floor((p.x() - m_cellOffset.x()) / m_cell.width())),
                          int(std::floor((p.y() - m_cellOffset.y()) / m_cell.height())));
        };
        const QPoint a = cellOf(m_anchorPos), b = cellOf(pos);
        const QRect cells(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                          QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
        const QRect r(m_cellOffset.x() + cells.x() * m_cell.width(), m_cellOffset.y() + cells.y() * m_cell.height(),
                      cells.width() * m_cell.width(), cells.height() * m_cell.height());
        return m_shape == easel::Selection::Shape::Ellipse ? easel::Selection::ellipse(r)
                                                           : easel::Selection::rect(r);
    }
    QPoint end = corner(pos);
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
        // Square / circle: the larger side, in the direction dragged.
        const int side = std::max(std::abs(end.x() - m_anchor.x()), std::abs(end.y() - m_anchor.y()));
        end = QPoint(m_anchor.x() + (end.x() < m_anchor.x() ? -side : side),
                     m_anchor.y() + (end.y() < m_anchor.y() ? -side : side));
    }
    const int x0 = std::min(m_anchor.x(), end.x()), x1 = std::max(m_anchor.x(), end.x());
    const int y0 = std::min(m_anchor.y(), end.y()), y1 = std::max(m_anchor.y(), end.y());
    if (x1 == x0 || y1 == y0)
        return {};
    const QRect r(x0, y0, x1 - x0, y1 - y0);
    return m_shape == easel::Selection::Shape::Ellipse ? easel::Selection::ellipse(r)
                                                       : easel::Selection::rect(r);
}

void SelectTool::press(const easel::StrokeSample &s)
{
    m_anchor = corner(s.pos);
    m_anchorPos = s.pos;
    m_active = true;
    emit selectionDragged(m_cell.isEmpty() ? easel::Selection() : selectionTo(s.pos));
}

void SelectTool::move(const easel::StrokeSample &s)
{
    if (m_active)
        emit selectionDragged(selectionTo(s.pos));
}

void SelectTool::release(const easel::StrokeSample &s)
{
    if (!m_active)
        return;
    m_active = false;
    emit selectionFinished(selectionTo(s.pos));
}

bool MoveTool::keyPress(QKeyEvent *event)
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

void WandTool::press(const easel::StrokeSample &s)
{
    emit clicked(s.pos, QGuiApplication::keyboardModifiers());
}
