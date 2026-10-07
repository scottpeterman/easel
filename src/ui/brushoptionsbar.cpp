#include "brushoptionsbar.h"

#include "brushtool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

#include <cmath>

using easeletch::BrushSettings;

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

    // Each group is its own toolbar item. A window too narrow for all of them
    // first squeezes the sliders, then moves whole groups, from the right,
    // behind the toolbar's own ">>" button: nothing is ever cut short.
    QWidget *host = beginGroup();
    m_mode = new QLabel(host);
    m_mode->setStyleSheet(QStringLiteral("font-weight: 600;"));
    host->layout()->addWidget(m_mode);
    addWidget(host);

    const auto sliderGroup = [this](const QString &label, int min, int max, const QString &suffix,
                                    std::function<int(int)> toSlider, std::function<int(int)> toSpin,
                                    QLabel **labelOut = nullptr) {
        QWidget *group = beginGroup();
        Control c = addControl(group, label, min, max, suffix, std::move(toSlider), std::move(toSpin), labelOut);
        c.slider->setMinimumWidth(MinSliderWidth);
        c.slider->setMaximumWidth(SliderWidth);
        addWidget(group);
        return c;
    };
    m_size = sliderGroup(tr("Size"), int(BrushSettings::MinSize), int(BrushSettings::MaxSize), tr(" px"),
                         sizeToSlider, sliderToSize);
    m_size.slider->setRange(0, 1000);
    m_opacity = sliderGroup(tr("Opacity"), 1, 100, tr("%"), identity, identity, &m_opacityLabel);
    m_hardness = sliderGroup(tr("Hardness"), 0, 100, tr("%"), identity, identity);
    m_stabilizer = sliderGroup(tr("Stabilizer"), 0, 100, tr("%"), identity, identity);

    host = beginGroup();
    m_pressureSize = new QCheckBox(tr("Pressure size"), host);
    m_pressureSize->setObjectName(QStringLiteral("brushPressureSize"));
    m_pressureSize->setToolTip(tr("Pressing harder with a pen paints a wider line"));
    m_pressureOpacity = new QCheckBox(tr("Pressure opacity"), host);
    m_pressureOpacity->setObjectName(QStringLiteral("brushPressureOpacity"));
    m_pressureOpacity->setToolTip(tr("Pressing harder with a pen paints a more solid line"));
    host->layout()->addWidget(m_pressureSize);
    host->layout()->addWidget(m_pressureOpacity);
    addWidget(host);

    host = beginGroup();
    m_pixel = new QCheckBox(tr("Pixel"), host);
    m_pixel->setToolTip(tr("Hard, unantialiased pixels: a 1 px brush sets exactly one pixel"));
    host->layout()->addWidget(m_pixel);
    addWidget(host);

    host = beginGroup();
    m_mirrorLabel = new QLabel(tr("Mirror"), host);
    m_mirror = new QComboBox(host);
    m_mirror->setObjectName(QStringLiteral("brushMirror"));
    m_mirror->addItems({tr("Off"), tr("Left / right"), tr("Top / bottom"), tr("Both")});
    m_mirror->setToolTip(tr("Paints the other side as you paint this one, mirrored across the line shown on the "
                            "canvas. For anything symmetrical: a ship from above, a face, a pattern."));
    host->layout()->addWidget(m_mirrorLabel);
    host->layout()->addWidget(m_mirror);
    addWidget(host);

    host = beginGroup();
    // Less-used settings behind "More".
    auto *more = new QToolButton(host);
    more->setObjectName(QStringLiteral("brushMore"));
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
    // Where the mirror's lines cross, in canvas pixels.
    auto *mirrorRow = new QWidget(panel);
    auto *mirrorLayout = new QHBoxLayout(mirrorRow);
    mirrorLayout->setContentsMargins(0, 0, 0, 0);
    m_mirrorX = new QSpinBox(mirrorRow);
    m_mirrorX->setObjectName(QStringLiteral("brushMirrorX"));
    m_mirrorY = new QSpinBox(mirrorRow);
    m_mirrorY->setObjectName(QStringLiteral("brushMirrorY"));
    for (QSpinBox *box : {m_mirrorX, m_mirrorY}) {
        box->setRange(-100000, 100000);
        box->setKeyboardTracking(false);
        box->setSuffix(tr(" px"));
    }
    m_mirrorX->setPrefix(tr("X "));
    m_mirrorY->setPrefix(tr("Y "));
    m_mirrorCentre = new QPushButton(tr("Centre"), mirrorRow);
    m_mirrorCentre->setObjectName(QStringLiteral("brushMirrorCentre"));
    m_mirrorCentre->setToolTip(tr("Back to the middle of the canvas"));
    mirrorLayout->addWidget(m_mirrorX);
    mirrorLayout->addWidget(m_mirrorY);
    mirrorLayout->addWidget(m_mirrorCentre);
    form->addRow(tr("Mirror at"), mirrorRow);
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    more->setMenu(menu);
    host->layout()->addWidget(more);
    addWidget(host);

    connect(m_size.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.size = v; }); });
    connect(m_opacity.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.opacity = v / 100.0; }); });
    connect(m_hardness.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.hardness = v / 100.0; }); });
    connect(m_stabilizer.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.stabilizer = v / 100.0; }); });
    connect(m_flow.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.flow = v / 100.0; }); });
    connect(m_spacing.spin, &QSpinBox::valueChanged, this, [this](int v) { apply([v](BrushSettings &b) { b.spacing = v / 100.0; }); });
    connect(m_pressureSize, &QCheckBox::toggled, this, [this](bool on) { apply([on](BrushSettings &b) { b.pressureSize = on; }); });
    connect(m_pressureOpacity, &QCheckBox::toggled, this, [this](bool on) { apply([on](BrushSettings &b) { b.pressureOpacity = on; }); });
    connect(m_pixel, &QCheckBox::toggled, this, [this](bool on) { apply([on](BrushSettings &b) { b.pixel = on; }); });

    connect(m_mirror, &QComboBox::activated, this, [this](int i) { m_tool->setSymmetry(easeletch::Symmetry(i)); });
    const auto moveAxis = [this] {
        if (!m_syncing)
            m_tool->setSymmetryAxis(QPointF(m_mirrorX->value(), m_mirrorY->value()));
    };
    connect(m_mirrorX, &QSpinBox::valueChanged, this, moveAxis);
    connect(m_mirrorY, &QSpinBox::valueChanged, this, moveAxis);
    connect(m_mirrorCentre, &QPushButton::clicked, m_tool, &BrushTool::centreSymmetryAxis);
    connect(m_tool, &BrushTool::symmetryChanged, this, &BrushOptionsBar::syncFromTool);

    connect(m_tool, &BrushTool::settingsChanged, this, &BrushOptionsBar::syncFromTool);
    syncFromTool();
}

QWidget *BrushOptionsBar::beginGroup()
{
    auto *group = new QWidget(this);
    auto *row = new QHBoxLayout(group);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(6);
    // Shrinks when the bar is short of room; never stretches to fill a wide one.
    group->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    return group;
}

BrushOptionsBar::Control BrushOptionsBar::addControl(QWidget *host, const QString &label, int min, int max,
                                                     const QString &suffix,
                                                     std::function<int(int)> spinToSlider,
                                                     std::function<int(int)> sliderToSpin,
                                                     QLabel **labelOut)
{
    auto *layout = qobject_cast<QHBoxLayout *>(host->layout());
    if (!layout) {
        layout = new QHBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
    }
    if (!label.isEmpty()) {
        auto *l = new QLabel(label, host);
        layout->addWidget(l);
        if (labelOut)
            *labelOut = l;
    }

    Control c;
    c.slider = new QSlider(Qt::Horizontal, host);
    c.slider->setRange(min, max);
    c.slider->setFixedWidth(SliderWidth);
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
    const easeletch::BrushMode mode = m_tool->mode();
    m_mode->setText(mode == easeletch::BrushMode::Erase    ? tr("Eraser")
                    : mode == easeletch::BrushMode::Smudge ? tr("Smudge")
                    : mode == easeletch::BrushMode::Clone  ? tr("Clone")
                    : mode == easeletch::BrushMode::Heal   ? tr("Heal")
                                                           : tr("Brush"));
    // For smudge, "opacity" is how far colour is dragged.
    m_opacityLabel->setText(mode == easeletch::BrushMode::Smudge ? tr("Strength") : tr("Opacity"));

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

    const QSignalBlocker ps(m_pressureSize), po(m_pressureOpacity), px(m_pixel);
    m_pressureSize->setChecked(b.pressureSize);
    m_pressureOpacity->setChecked(b.pressureOpacity);
    m_pixel->setChecked(b.pixel);

    // Mirroring is the brush's and the eraser's.
    const bool mirrors = mode == easeletch::BrushMode::Paint || mode == easeletch::BrushMode::Erase;
    const QSignalBlocker mm(m_mirror), mx(m_mirrorX), my(m_mirrorY);
    m_mirror->setCurrentIndex(int(m_tool->symmetry()));
    m_mirrorLabel->setEnabled(mirrors);
    m_mirror->setEnabled(mirrors);
    const QPointF axis = m_tool->symmetryAxis();
    m_mirrorX->setValue(int(std::lround(axis.x())));
    m_mirrorY->setValue(int(std::lround(axis.y())));
    const bool on = mirrors && m_tool->symmetry() != easeletch::Symmetry::Off;
    m_mirrorX->setEnabled(on && m_tool->symmetry() != easeletch::Symmetry::TopBottom);
    m_mirrorY->setEnabled(on && m_tool->symmetry() != easeletch::Symmetry::LeftRight);
    m_mirrorCentre->setEnabled(on && !m_tool->symmetryAxisCentred());
    m_syncing = false;
}
