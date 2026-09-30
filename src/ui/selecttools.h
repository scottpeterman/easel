#pragma once

#include "canvasview.h"
#include "selection.h"

#include <QObject>
#include <QPoint>
#include <QSize>

// Drag out a rectangle or ellipse selection. Shift keeps it square / circular.
// A click without dragging clears the selection. With a snap grid set, the
// selection covers whole cells: a click selects the cell under it.
class SelectTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    explicit SelectTool(easel::Selection::Shape shape, QObject *parent = nullptr);

    easel::Selection::Shape shape() const { return m_shape; }
    // An empty cell size turns snapping off.
    void setSnapGrid(const QSize &cell, const QPoint &offset)
    {
        m_cell = cell;
        m_cellOffset = offset;
    }

    void press(const easel::StrokeSample &s) override;
    void move(const easel::StrokeSample &s) override;
    void release(const easel::StrokeSample &s) override;
    double cursorDiameter() const override { return 0.0; }

signals:
    // While dragging (for the live outline), and when the drag ends.
    void selectionDragged(const easel::Selection &selection);
    void selectionFinished(const easel::Selection &selection);

private:
    easel::Selection selectionTo(const QPointF &pos) const;

    easel::Selection::Shape m_shape;
    QPoint m_anchor;
    QPointF m_anchorPos;
    QSize m_cell;
    QPoint m_cellOffset;
    bool m_active = false;
};

// Drags selected pixels (or pasted content) around; arrow keys nudge by one
// pixel, ten with Shift. The window does the moving: this reports gestures.
class MoveTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    void press(const easel::StrokeSample &s) override { emit dragStarted(s.pos); }
    void move(const easel::StrokeSample &s) override { emit dragged(s.pos); }
    void release(const easel::StrokeSample &s) override { emit dragEnded(s.pos); }
    double cursorDiameter() const override { return 0.0; }
    Qt::CursorShape cursorShape() const override { return Qt::SizeAllCursor; }
    bool keyPress(QKeyEvent *event) override;

signals:
    void dragStarted(const QPointF &canvasPos);
    void dragged(const QPointF &canvasPos);
    void dragEnded(const QPointF &canvasPos);
    void nudged(const QPoint &delta);
};

// Magic wand: a click selects pixels of similar colour. Shift adds to the
// selection, Ctrl takes away from it. The window does the selecting.
class WandTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    double tolerance() const { return m_tolerance; }
    void setTolerance(double t) { m_tolerance = t; }
    bool contiguous() const { return m_contiguous; }
    void setContiguous(bool on) { m_contiguous = on; }

    void press(const easel::StrokeSample &s) override;
    void move(const easel::StrokeSample &) override {}
    void release(const easel::StrokeSample &) override {}
    double cursorDiameter() const override { return 0.0; }

signals:
    void clicked(const QPointF &canvasPos, Qt::KeyboardModifiers modifiers);

private:
    double m_tolerance = 0.12;
    bool m_contiguous = true;
};
