#include "brushoptionsbar.h"

#include "brushtool.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

#include <cmath>

using easel::BrushSettings;

namespace {

// Size slider is logarithmic: 1 px at 0, 1000 px at 1000.
int sizeToSlider(int size)
{
    return int(std::lround(std::log(double(std::max(size, 1))) / std::log(1000.0) * 1000.0));
}

int sliderToSize(int v)
{
    return int(std::lround(std::pow(1000.0, v / 1000.0)));
}

int identity(int v)
{
    return v;
}

int percent(double v)
{
    return int(std::lround(v * 100.0));
}

} // namespace

BrushOptionsBar::BrushOptionsBar(BrushTool *tool, QWidget *parent)
    : QToolBar(tr("Tool Options"), parent)
    , m_tool(tool)
{
    setObjectName(QStringLiteral("ToolOptionsBar"));
    setMovable(false);

    auto *host = new QWidget(this);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);

    m_mode = new QLabel(host);
    m_mode->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(m_mode);
    row->addSpacing(8);

    m_size = addControl(host, tr("Size"), int(BrushSettings::MinSize), int(BrushSettings::MaxSize),
                        tr(" px"), sizeToSlider, sliderToSize);
    m_size.slider->setRange(0, 1000);
    m_opacity = addControl(host, tr("Opacity"), 1, 100, tr("%"), identity, identity);
    m_hardness = addControl(host, tr("Hardness"), 0, 100, tr("%"), identity, identity);
    m_stabilizer = addControl(host, tr("Stabilizer"), 0, 100, tr("%"), identity, identity);

    m_pressureSize = new QCheckBox(tr("Pressure size"), host);
    m_pressureOpacity = new QCheckBox(tr("Pressure opacity"), host);
    row->addWidget(m_pressureSize);
    row->addWidget(m_pressureOpacity);

    // Less-used settings behind "More".
    auto *more = new QToolButton(host);
    more->setText(tr("More"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(more);
    auto *panel = new QWidget(menu);
    auto *form = new QFormLayout(panel);
    auto *flowRow = new QWidget(panel);
    auto *spacingRow = new QWidget(panel);
    m_flow = addControl(flowRow, QString(), 1, 100, tr("%"), identity, identity);
    m_spacing = addControl(spacingRow, QString(), 1, 200, tr("%"), identity, identity);
    form->addRow(tr("Flow"), flowRow);
    form->addRow(tr("Spacing"), spacingRow);
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    more->setMenu(menu);
    row->addWidget(more);
    row->addStretch(1);

    addWidget(host);

    connect(m_size.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.size = v; }); });
    connect(m_opacity.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.opacity = v / 100.0; }); });
    connect(m_hardness.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.hardness = v / 100.0; }); });
    connect(m_stabilizer.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.stabilizer = v / 100.0; }); });
    connect(m_flow.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.flow = v / 100.0; }); });
    connect(m_spacing.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.spacing = v / 100.0; }); });
    connect(m_pressureSize, &QCheckBox::toggled, this, [this](bool on) { apply([on](BrushSettings &b) { b.pressureSize = on; }); });
    connect(m_pressureOpacity, &QCheckBox::toggled, this, [this](bool on) { apply([on](BrushSettings &b) { b.pressureOpacity = on; }); });

    connect(m_tool, &BrushTool::settingsChanged, this, &BrushOptionsBar::syncFromTool);
    syncFromTool();
}

BrushOptionsBar::Control BrushOptionsBar::addControl(QWidget *host, const QString &label, int min, int max,
                                                     const QString &suffix,
                                                     std::function<int(int)> spinToSlider,
                                                     std::function<int(int)> sliderToSpin)
{
    auto *layout = qobject_cast<QHBoxLayout *>(host->layout());
    if (!layout) {
        layout = new QHBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
    }
    if (!label.isEmpty())
        layout->addWidget(new QLabel(label, host));

    Control c;
    c.slider = new QSlider(Qt::Horizontal, host);
    c.slider->setRange(min, max);
    c.slider->setFixedWidth(90);
    c.spin = new QSpinBox(host);
    c.spin->setRange(min, max);
    c.spin->setSuffix(suffix);
    c.spin->setKeyboardTracking(false);
    layout->addWidget(c.slider);
    layout->addWidget(c.spin);

    QSpinBox *spin = c.spin;
    QSlider *slider = c.slider;
    connect(slider, &QSlider::valueChanged, spin, [spin, sliderToSpin](int v) { spin->setValue(sliderToSpin(v)); });
    connect(spin, &QSpinBox::valueChanged, slider, [slider, spinToSlider](int v) {
        const QSignalBlocker block(slider);
        slider->setValue(spinToSlider(v));
    });
    return c;
}

void BrushOptionsBar::apply(const std::function<void(BrushSettings &)> &change)
{
    if (m_syncing)
        return;
    BrushSettings b = m_tool->settings();
    change(b);
    m_tool->setSettings(b);
}

void BrushOptionsBar::syncFromTool()
{
    m_syncing = true;
    const BrushSettings b = m_tool->settings();
    m_mode->setText(m_tool->mode() == easel::BrushMode::Erase ? tr("Eraser") : tr("Brush"));

    const auto set = [](Control &c, int spinValue, int sliderValue) {
        const QSignalBlocker a(c.spin), s(c.slider);
        c.spin->setValue(spinValue);
        c.slider->setValue(sliderValue);
    };
    const int size = int(std::lround(b.size));
    set(m_size, size, sizeToSlider(size));
    set(m_opacity, percent(b.opacity), percent(b.opacity));
    set(m_hardness, percent(b.hardness), percent(b.hardness));
    set(m_stabilizer, percent(b.stabilizer), percent(b.stabilizer));
    set(m_flow, percent(b.flow), percent(b.flow));
    set(m_spacing, percent(b.spacing), percent(b.spacing));

    const QSignalBlocker ps(m_pressureSize), po(m_pressureOpacity);
    m_pressureSize->setChecked(b.pressureSize);
    m_pressureOpacity->setChecked(b.pressureOpacity);
    m_syncing = false;
}
