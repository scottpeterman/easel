#pragma once

#include "canvasview.h"

#include <QObject>
#include <QPoint>
#include <QPointF>

// Free transform: drag a handle to scale, outside the box to rotate, inside
// it to move; arrows nudge. The window does the transforming: this reports
// gestures.
class TransformTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    void press(const easeletch::StrokeSample &s) override { emit pressed(s.pos); }
    void move(const easeletch::StrokeSample &s) override { emit dragged(s.pos); }
    void release(const easeletch::StrokeSample &s) override { emit released(s.pos); }
    double cursorDiameter() const override { return 0.0; }
    Qt::CursorShape cursorShape() const override { return Qt::ArrowCursor; }
    bool keyPress(QKeyEvent *event) override;

signals:
    void pressed(const QPointF &canvasPos);
    void dragged(const QPointF &canvasPos);
    void released(const QPointF &canvasPos);
    void nudged(const QPoint &delta);
};
