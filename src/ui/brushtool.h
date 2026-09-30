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

    void setDocument(easel::TileStore *store, const QRect &bounds, easel::History *history);
    // Strokes stay inside this selection when it isn't empty.
    void setSelection(const easel::Selection *selection) { m_selection = selection; }

    easel::BrushMode mode() const { return m_mode; }
    void setMode(easel::BrushMode mode);

    // Settings of the current mode.
    easel::BrushSettings settings() const;
    void setSettings(const easel::BrushSettings &settings);
    void scaleSize(double factor);

    QColor color() const { return m_color; }
    void setColor(const QColor &color) { m_color = color; }

    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

    // CanvasTool
    void press(const easel::StrokeSample &s) override;
    void move(const easel::StrokeSample &s) override;
    void release(const easel::StrokeSample &s) override;
    double cursorDiameter() const override;

signals:
    void modeChanged(easel::BrushMode mode);
    void settingsChanged();
    void strokeCommitted();

private:
    easel::BrushSettings &current();

    easel::TileStore *m_store = nullptr;
    QRect m_bounds;
    easel::History *m_history = nullptr;

    easel::BrushMode m_mode = easel::BrushMode::Paint;
    easel::BrushSettings m_paint;
    easel::BrushSettings m_erase;
    easel::BrushSettings m_smudge;
    const easel::Selection *m_selection = nullptr;
    QColor m_color = Qt::black;
    easel::BrushStroke m_stroke;
};
