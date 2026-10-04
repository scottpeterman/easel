#pragma once

#include "brush.h"
#include "canvasview.h"
#include "history.h"

#include <QColor>
#include <QObject>
#include <QRect>

class QSettings;

// Brush, eraser, smudge, clone and heal. Each mode keeps its own settings
// (size, opacity, ...). A finished stroke is recorded in the document's history.
//
// Clone paints with a copy of the layer from somewhere else on it. The source
// is set first (Alt+click, see CloneSourceTool); the first stroke after that
// fixes how far the copy is from the brush, and later strokes keep that
// distance, so separate strokes fill in one continuous copy.
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

    // Clone: the point to copy from. The next stroke starts copying there.
    void setCloneSource(const QPointF &pos);
    bool hasCloneSource() const { return m_hasSource; }
    // Where the copy is taken from at the moment: the source point itself
    // until a stroke has been made, then the point matching the last press.
    QPointF cloneSource() const { return m_source; }

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
    // A clone stroke was started before a source was chosen.
    void cloneSourceNeeded();
    void cloneSourceChanged();

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
    easeletch::BrushSettings m_clone;
    easeletch::BrushSettings m_heal;
    bool m_hasSource = false;
    bool m_offsetFixed = false;
    QPointF m_source;
    QPoint m_offset; // source minus brush, once a stroke has fixed it
    const easeletch::Selection *m_selection = nullptr;
    QColor m_color = Qt::black;
    easeletch::BrushStroke m_stroke;
};

// Chooses the Clone tool's source: used in place of the eyedropper while Alt
// is held and the Clone tool is active.
class CloneSourceTool : public QObject, public CanvasTool
{
    Q_OBJECT

public:
    explicit CloneSourceTool(BrushTool *brush, QObject *parent = nullptr)
        : QObject(parent)
        , m_brush(brush)
    {
    }

    void press(const easeletch::StrokeSample &s) override { m_brush->setCloneSource(s.pos); }
    void move(const easeletch::StrokeSample &) override {}
    void release(const easeletch::StrokeSample &) override {}
    double cursorDiameter() const override { return 0.0; }

private:
    BrushTool *m_brush;
};
