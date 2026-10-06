#pragma once

#include "brush.h"

#include <QToolBar>

#include <functional>

class BrushTool;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QSlider;
class QSpinBox;

// Tool options for the brush and eraser: size, opacity, hardness, stabilizer,
// pressure and mirroring up front; flow, spacing and where the mirror line
// sits under "More".
class BrushOptionsBar : public QToolBar
{
    Q_OBJECT

public:
    explicit BrushOptionsBar(BrushTool *tool, QWidget *parent = nullptr);

private:
    struct Control {
        QSlider *slider = nullptr;
        QSpinBox *spin = nullptr;
    };

    Control addControl(QWidget *host, const QString &label, int min, int max, const QString &suffix,
                       std::function<int(int)> spinToSlider, std::function<int(int)> sliderToSpin,
                       QLabel **labelOut = nullptr);
    void apply(const std::function<void(easeletch::BrushSettings &)> &change);
    void syncFromTool();

    BrushTool *m_tool;
    bool m_syncing = false;

    QLabel *m_mode = nullptr;
    Control m_size, m_opacity, m_hardness, m_stabilizer, m_flow, m_spacing;
    QCheckBox *m_pressureSize = nullptr;
    QCheckBox *m_pressureOpacity = nullptr;
    QCheckBox *m_pixel = nullptr;
    QLabel *m_opacityLabel = nullptr;
    QLabel *m_mirrorLabel = nullptr;
    QComboBox *m_mirror = nullptr;
    QSpinBox *m_mirrorX = nullptr;
    QSpinBox *m_mirrorY = nullptr;
    QPushButton *m_mirrorCentre = nullptr;
};
