#pragma once

#include "shade.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QSpinBox;
class QTimer;

// Shade Areas' settings. The canvas behind shows the result as they change
// (the window does the shading: this reports the settings).
class ShadeDialog : public QDialog
{
    Q_OBJECT

public:
    // tintColour: what "Tint" colours the gradient with (the current colour).
    ShadeDialog(const easeletch::ShadeSettings &settings, const easeletch::AreaOptions &options, int gradient,
                const QColor &tintColour, QWidget *parent = nullptr);

    easeletch::ShadeSettings settings() const { return m_settings; }
    easeletch::AreaOptions options() const { return m_options; }
    // Which gradient is chosen: 0 is the soft shading, then the presets in order.
    int gradientIndex() const;
    bool previewEnabled() const;
    // The gradient for an index, as the list shows them.
    static easeletch::GradientStops gradientAt(int index);

signals:
    // The settings to show on the canvas now, sent shortly after the last change.
    void previewRequested(const easeletch::ShadeSettings &settings, const easeletch::AreaOptions &options);
    void previewCleared();

private:
    struct Row {
        QSlider *slider = nullptr;
        QSpinBox *spin = nullptr;
    };
    Row addRow(class QGridLayout *grid, const QString &label, int min, int max, const QString &suffix, const QString &tip);
    void changed();

    easeletch::ShadeSettings m_settings;
    easeletch::AreaOptions m_options;
    QColor m_tintColour;
    QComboBox *m_gradient = nullptr;
    QCheckBox *m_tint = nullptr;
    Row m_angle, m_brightness, m_variation;
    QSpinBox *m_minArea = nullptr;
    QCheckBox *m_whiteOnly = nullptr;
    QCheckBox *m_preview = nullptr;
    QTimer *m_timer = nullptr;
    bool m_syncing = false;
};
