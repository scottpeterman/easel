#pragma once

#include "brush.h"
#include "brushpresets.h"
#include "canvasview.h"
#include "history.h"

#include <QColor>
#include <QObject>
#include <QRect>

class QSettings;

// Brush, eraser, smudge, clone and heal. Each mode keeps its own settings
// (size, opacity, ...). A finished stroke is recorded in the document's history.
//
// A press with Shift held draws a straight line to it from where the last
// stroke ended, and carries on from there if it's dragged.
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

    // The ready-made brush the Brush tool was last set to (see brushpresets.h).
    // Choosing one replaces the Brush tool's settings with the preset's, whatever
    // mode is in use; the settings can be changed from there as usual.
    QString preset() const { return m_preset; }
    QString presetName() const;
    // False if there's no such preset.
    bool setPreset(const QString &id);

    QColor color() const { return m_color; }
    void setColor(const QColor &color) { m_color = color; }

    // Brush and eraser strokes mirrored across a line (or two) as they're
    // made. The lines cross at the middle of the canvas until they're put
    // somewhere else.
    easeletch::Symmetry symmetry() const { return m_symmetry; }
    void setSymmetry(easeletch::Symmetry symmetry);
    QPointF symmetryAxis() const;
    void setSymmetryAxis(const QPointF &axis);
    // Back to the middle of the canvas, and following it from one canvas to the next.
    void centreSymmetryAxis();
    bool symmetryAxisCentred() const { return m_axisCentred; }
    // The mirroring applies to the mode in use (brush and eraser only).
    bool symmetryActive() const;
    QRect bounds() const { return m_bounds; }
    // Where the last stroke ended, for Shift+click; false until there's been one.
    bool lastStrokeEnd(QPointF *pos) const;

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
    void presetChanged(const QString &id);
    void strokeCommitted();
    // A stroke was started with nothing to paint on.
    void blocked();
    // A stroke was started outside the selection, where it leaves no mark.
    void outsideSelection();
    // A clone stroke was started before a source was chosen.
    void cloneSourceNeeded();
    void cloneSourceChanged();
    void symmetryChanged();

private:
    easeletch::BrushSettings &current();

    easeletch::TileStore *m_store = nullptr;
    QRect m_bounds;
    easeletch::History *m_history = nullptr;
    int m_layerId = 0;
    bool m_layerMask = false;

    easeletch::BrushMode m_mode = easeletch::BrushMode::Paint;
    easeletch::BrushSettings m_paint;
    QString m_preset = easeletch::defaultBrushPresetId();
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
    easeletch::Symmetry m_symmetry = easeletch::Symmetry::Off;
    bool m_axisCentred = true;
    QPointF m_axis;
    bool m_hasLastEnd = false;
    QPointF m_lastEnd;
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
