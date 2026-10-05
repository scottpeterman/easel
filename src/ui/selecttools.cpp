#include "selecttools.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>

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

SelectTool::SelectTool(easeletch::Selection::Shape shape, QObject *parent)
    : QObject(parent)
    , m_shape(shape)
{
}

easeletch::Selection SelectTool::selectionTo(const QPointF &pos) const
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
        return m_shape == easeletch::Selection::Shape::Ellipse ? easeletch::Selection::ellipse(r)
                                                           : easeletch::Selection::rect(r);
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
    return m_shape == easeletch::Selection::Shape::Ellipse ? easeletch::Selection::ellipse(r)
                                                       : easeletch::Selection::rect(r);
}

void SelectTool::press(const easeletch::StrokeSample &s)
{
    m_anchor = corner(s.pos);
    m_anchorPos = s.pos;
    m_active = true;
    emit selectionDragged(m_cell.isEmpty() ? easeletch::Selection() : selectionTo(s.pos));
}

void SelectTool::move(const easeletch::StrokeSample &s)
{
    if (m_active)
        emit selectionDragged(selectionTo(s.pos));
}

void SelectTool::release(const easeletch::StrokeSample &s)
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

void WandTool::press(const easeletch::StrokeSample &s)
{
    emit clicked(s.pos, QGuiApplication::keyboardModifiers());
}

void LassoTool::press(const easeletch::StrokeSample &s)
{
    m_ignore = false;
    m_dragged = false;
    m_pressPos = s.pos;
    if (m_path.isEmpty()) {
        m_fresh = true;
        m_path << s.pos;
    } else {
        m_fresh = false;
        if (m_path.size() >= 3 && QLineF(s.pos, m_path.first()).length() <= m_closeDistance) {
            m_ignore = true;
            finish();
            return;
        }
        m_path << s.pos;
    }
    emit pathChanged(m_path);
}

void LassoTool::move(const easeletch::StrokeSample &s)
{
    if (m_ignore || m_path.isEmpty())
        return;
    if (QLineF(s.pos, m_pressPos).length() > m_closeDistance)
        m_dragged = true;
    if (QLineF(s.pos, m_path.last()).length() < 0.25)
        return;
    m_path << s.pos;
    emit pathChanged(m_path);
}

void LassoTool::release(const easeletch::StrokeSample &s)
{
    Q_UNUSED(s);
    if (m_ignore) {
        m_ignore = false;
        return;
    }
    // One drag from start to end is a freehand lasso: letting go closes it.
    // A click only places a point.
    if (m_fresh && m_dragged)
        finish();
}

void LassoTool::finish()
{
    if (m_path.size() < 3) {
        cancel();
        return;
    }
    const QPolygonF path = m_path;
    m_path.clear();
    emit finished(path, QGuiApplication::keyboardModifiers());
}

void LassoTool::cancel()
{
    if (m_path.isEmpty())
        return;
    m_path.clear();
    emit pathChanged(m_path);
}

bool ShapeTool::keyPress(QKeyEvent *event)
{
    if (!m_working || (event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete))
        return false;
    emit key(event->key());
    return true;
}
