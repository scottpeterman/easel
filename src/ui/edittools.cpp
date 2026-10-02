#include "edittools.h"

#include <QKeyEvent>

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
