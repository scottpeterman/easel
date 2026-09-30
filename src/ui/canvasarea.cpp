#include "canvasarea.h"

#include "canvasview.h"

#include <QGridLayout>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

CanvasArea::CanvasArea(CanvasView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_h(new QScrollBar(Qt::Horizontal, this))
    , m_v(new QScrollBar(Qt::Vertical, this))
{
    // Clicking a bar shouldn't take keyboard focus from the canvas (Space, Alt).
    m_h->setFocusPolicy(Qt::NoFocus);
    m_v->setFocusPolicy(Qt::NoFocus);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(m_view, 0, 0);
    grid->addWidget(m_v, 0, 1);
    grid->addWidget(m_h, 1, 0);

    connect(m_view, &CanvasView::viewChanged, this, &CanvasArea::syncBars);
    connect(m_h, &QScrollBar::valueChanged, this, &CanvasArea::barMoved);
    connect(m_v, &QScrollBar::valueChanged, this, &CanvasArea::barMoved);
    syncBars();
}

void CanvasArea::syncBars()
{
    if (m_syncing)
        return;
    m_syncing = true;

    const QPointF pan = m_view->pan();
    const QRectF atZero = m_view->canvasViewBounds().translated(-pan);
    const double w = m_view->width(), h = m_view->height();

    // Pan limits that put the right edge / left edge of the canvas at the
    // middle of the view; widened to include wherever you've panned to.
    const auto axis = [](double viewLen, double lo, double hi, double current, double &origin,
                         QScrollBar *bar) {
        const double minPan = std::min(viewLen / 2.0 - hi, current);
        const double maxPan = std::max(viewLen / 2.0 - lo, current);
        origin = std::round(maxPan);
        bar->setRange(0, int(std::round(maxPan - minPan)));
        bar->setPageStep(std::max(1, int(viewLen)));
        bar->setSingleStep(std::max(1, int(viewLen / 20.0)));
        bar->setValue(int(std::round(origin - current)));
    };
    axis(w, atZero.left(), atZero.right(), pan.x(), m_originX, m_h);
    axis(h, atZero.top(), atZero.bottom(), pan.y(), m_originY, m_v);

    m_syncing = false;
}

void CanvasArea::barMoved()
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_view->setPan(QPointF(m_originX - m_h->value(), m_originY - m_v->value()));
    m_syncing = false;
}
