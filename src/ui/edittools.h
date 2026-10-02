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

// Fill: a click floods the area of similar colour under it with the painting
// colour. The window does the filling.
class FillTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    void press(const easeletch::StrokeSample &s) override { emit clicked(s.pos); }
    void move(const easeletch::StrokeSample &) override {}
    void release(const easeletch::StrokeSample &) override {}
    double cursorDiameter() const override { return 0.0; }

signals:
    void clicked(const QPointF &canvasPos);
};

// Gradient: drag from where it starts to where it ends. Shift keeps the line
// to 45 degree steps. The window draws it when the drag ends.
class GradientTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    void press(const easeletch::StrokeSample &s) override;
    void move(const easeletch::StrokeSample &s) override;
    void release(const easeletch::StrokeSample &s) override;
    double cursorDiameter() const override { return 0.0; }

signals:
    // While dragging, for the guide line; an empty drag (from == to) clears it.
    void dragged(const QPointF &from, const QPointF &to);
    void finished(const QPointF &from, const QPointF &to);

private:
    QPointF endFor(const QPointF &pos) const;

    QPointF m_from;
    bool m_active = false;
};
