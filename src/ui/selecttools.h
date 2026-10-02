#pragma once

#include "canvasview.h"
#include "selection.h"

#include <QObject>
#include <QPoint>
#include <QPolygonF>
#include <QSize>

// Drag out a rectangle or ellipse selection. Shift keeps it square / circular.
// A click without dragging clears the selection. With a snap grid set, the
// selection covers whole cells: a click selects the cell under it.
class SelectTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    explicit SelectTool(easeletch::Selection::Shape shape, QObject *parent = nullptr);

    easeletch::Selection::Shape shape() const { return m_shape; }
    // An empty cell size turns snapping off.
    void setSnapGrid(const QSize &cell, const QPoint &offset)
    {
        m_cell = cell;
        m_cellOffset = offset;
    }

    void press(const easeletch::StrokeSample &s) override;
    void move(const easeletch::StrokeSample &s) override;
    void release(const easeletch::StrokeSample &s) override;
    double cursorDiameter() const override { return 0.0; }

signals:
    // While dragging (for the live outline), and when the drag ends.
    void selectionDragged(const easeletch::Selection &selection);
    void selectionFinished(const easeletch::Selection &selection);

private:
    easeletch::Selection selectionTo(const QPointF &pos) const;

    easeletch::Selection::Shape m_shape;
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

    void press(const easeletch::StrokeSample &s) override { emit dragStarted(s.pos); }
    void move(const easeletch::StrokeSample &s) override { emit dragged(s.pos); }
    void release(const easeletch::StrokeSample &s) override { emit dragEnded(s.pos); }
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

    void press(const easeletch::StrokeSample &s) override;
    void move(const easeletch::StrokeSample &) override {}
    void release(const easeletch::StrokeSample &) override {}
    double cursorDiameter() const override { return 0.0; }

signals:
    void clicked(const QPointF &canvasPos, Qt::KeyboardModifiers modifiers);

private:
    double m_tolerance = 0.12;
    bool m_contiguous = true;
};

// Lasso: drag to draw around something, and let go to close the shape. Or
// click point by point for straight edges (dragging between clicks draws
// freehand), then click the first point or press Enter to close; Escape gives
// up. The window turns the path into a selection: Shift adds, Ctrl subtracts.
class LassoTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    using QObject::QObject;

    // How near the first point a click closes the shape, in canvas pixels.
    void setCloseDistance(double distance) { m_closeDistance = distance; }
    // A path has been started and not closed yet.
    bool isOpen() const { return !m_path.isEmpty(); }
    QPolygonF path() const { return m_path; }
    // Closes the path (if it has three points) / throws it away.
    void finish();
    void cancel();

    void press(const easeletch::StrokeSample &s) override;
    void move(const easeletch::StrokeSample &s) override;
    void release(const easeletch::StrokeSample &s) override;
    double cursorDiameter() const override { return 0.0; }

signals:
    // The path so far, for drawing; empty when there is none.
    void pathChanged(const QPolygonF &path);
    void finished(const QPolygonF &path, Qt::KeyboardModifiers modifiers);

private:
    QPolygonF m_path;
    QPointF m_pressPos;
    double m_closeDistance = 6.0;
    bool m_fresh = false;   // this press started the path
    bool m_dragged = false; // ... and has moved since
    bool m_ignore = false;  // the press closed the path; skip its move and release
};
