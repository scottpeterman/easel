#pragma once

#include "brush.h"
#include "canvasview.h"
#include "history.h"

#include <QColor>
#include <QObject>
#include <QRect>

class QSettings;

// Brush, eraser and smudge. Each mode keeps its own settings (size, opacity, ...).
// A finished stroke is recorded in the document's history.
class BrushTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    explicit BrushTool(QObject *parent = nullptr);

    // store: the layer strokes go to; null when there's nothing to paint on
    // (a locked or hidden layer, a group). layerId names it in the history.
    // mask: store is the layer's mask, not its pixels.
    void setDocument(easeletch::TileStore *store, const QRect &bounds, easeletch::History *history, int layerId = 0,
                     bool mask = false);
    // Strokes stay inside this selection when it isn't empty.
    void setSelection(const easeletch::Selection *selection) { m_selection = selection; }

    easeletch::BrushMode mode() const { return m_mode; }
    void setMode(easeletch::BrushMode mode);

    // Settings of the current mode.
    easeletch::BrushSettings settings() const;
    void setSettings(const easeletch::BrushSettings &settings);
    void scaleSize(double factor);

    QColor color() const { return m_color; }
    void setColor(const QColor &color) { m_color = color; }

    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

    // CanvasTool
    void press(const easeletch::StrokeSample &s) override;
    void move(const easeletch::StrokeSample &s) override;
    void release(const easeletch::StrokeSample &s) override;
    double cursorDiameter() const override;

signals:
    void modeChanged(easeletch::BrushMode mode);
    void settingsChanged();
    void strokeCommitted();
    // A stroke was started with nothing to paint on.
    void blocked();
    // A stroke was started outside the selection, where it leaves no mark.
    void outsideSelection();

private:
    easeletch::BrushSettings &current();

    easeletch::TileStore *m_store = nullptr;
    QRect m_bounds;
    easeletch::History *m_history = nullptr;
    int m_layerId = 0;
    bool m_layerMask = false;

    easeletch::BrushMode m_mode = easeletch::BrushMode::Paint;
    easeletch::BrushSettings m_paint;
    easeletch::BrushSettings m_erase;
    easeletch::BrushSettings m_smudge;
    const easeletch::Selection *m_selection = nullptr;
    QColor m_color = Qt::black;
    easeletch::BrushStroke m_stroke;
};
