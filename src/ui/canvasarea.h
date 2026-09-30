#pragma once

#include <QWidget>

class CanvasView;
class QScrollBar;

// The canvas with scrollbars. The scrollable range lets any edge of the canvas
// (its bounding box, when rotated) travel to the middle of the view, so you can
// always centre the part you're working on. The bars follow every pan, zoom,
// rotation and resize, including panning with Space or the middle button.
class CanvasArea : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasArea(CanvasView *view, QWidget *parent = nullptr);

    CanvasView *view() const { return m_view; }
    QScrollBar *horizontalBar() const { return m_h; }
    QScrollBar *verticalBar() const { return m_v; }

private:
    void syncBars();
    void barMoved();

    CanvasView *m_view;
    QScrollBar *m_h;
    QScrollBar *m_v;
    bool m_syncing = false;
    // Pan values at scrollbar value 0; bar value = origin - pan.
    double m_originX = 0.0;
    double m_originY = 0.0;
};
