#pragma once

#include "brush.h"

#include <QToolBar>

#include <functional>

class BrushTool;
class QCheckBox;
class QLabel;
class QSlider;
class QSpinBox;

// Tool options for the brush and eraser: size, opacity, hardness, stabilizer
// and pressure up front; flow and spacing under "More".
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
                       std::function<int(int)> spinToSlider, std::function<int(int)> sliderToSpin);
    void apply(const std::function<void(easel::BrushSettings &)> &change);
    void syncFromTool();

    BrushTool *m_tool;
    bool m_syncing = false;

    QLabel *m_mode = nullptr;
    Control m_size, m_opacity, m_hardness, m_stabilizer, m_flow, m_spacing;
    QCheckBox *m_pressureSize = nullptr;
    QCheckBox *m_pressureOpacity = nullptr;
};
