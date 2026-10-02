#pragma once

#include "canvasview.h"
#include "tilestore.h"

#include <QColor>
#include <QObject>
#include <QRect>

// Picks the painting colour from the canvas. Samples what's on screen (the
// composite of the layers, not just the active one). Transparent pixels are
// skipped, and picked colours are opaque. Picking never touches history.
class EyedropperTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    explicit EyedropperTool(QObject *parent = nullptr);

    void setDocument(const easel::TileStore *store, const QRect &bounds);
    // The colour before picking starts, shown in the preview ring.
    void setCurrentColor(const QColor &color);

    // CanvasTool
    void press(const easel::StrokeSample &s) override;
    void move(const easel::StrokeSample &s) override;
    void release(const easel::StrokeSample &s) override;
    double cursorDiameter() const override { return 0.0; }
    bool colorPreview(QColor *picked, QColor *previous) const override;

signals:
    void colorPicked(const QColor &color);

private:
    void pick(const QPointF &pos);

    const easel::TileStore *m_store = nullptr;
    QRect m_bounds;
    QColor m_current = Qt::black;
    QColor m_previous;
    QColor m_picked;
    bool m_active = false;
};
