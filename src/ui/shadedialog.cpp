#include "shadedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using easeletch::AreaOptions;
using easeletch::ShadeSettings;

easeletch::GradientStops ShadeDialog::gradientAt(int index)
{
    const QList<easeletch::GradientPreset> &presets = easeletch::gradientPresets();
    if (index >= 1 && index <= presets.size())
        return presets.at(index - 1).stops;
    return ShadeSettings::softShadeStops();
}

ShadeDialog::ShadeDialog(const ShadeSettings &settings, const AreaOptions &options, int gradient, const QColor &tintColour,
                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings.normalized())
    , m_options(options)
    , m_tintColour(tintColour)
{
    setWindowTitle(tr("Shade Areas"));
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_preview->isChecked())
            emit previewRequested(m_settings, m_options);
    });

    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);

    m_gradient = new QComboBox(this);
    m_gradient->addItem(tr("Soft shading"));
    for (const easeletch::GradientPreset &p : easeletch::gradientPresets())
        m_gradient->addItem(p.name);
    m_gradient->setCurrentIndex(qBound(0, gradient, m_gradient->count() - 1));
    m_gradient->setToolTip(tr("The colours across each area. Soft shading is a plain surface in even light; "
                              "the metals have the bright and dark bands of a reflection."));
    grid->addWidget(new QLabel(tr("Gradient"), this), 0, 0);
    grid->addWidget(m_gradient, 0, 1, 1, 2);

    m_tint = new QCheckBox(tr("Tint with the current colour"), this);
    m_tint->setChecked(m_settings.tint.isValid());
    m_tint->setToolTip(tr("Colours the gradient: red for red metal, yellow for brass. "
                          "The brightest highlights stay near white."));
    {
        // A swatch of the colour it would use.
        QPixmap swatch(14, 14);
        swatch.fill(m_tintColour);
        m_tint->setIcon(QIcon(swatch));
    }
    grid->addWidget(m_tint, 1, 1, 1, 2);

    m_angle = addRow(grid, tr("Angle"), 0, 359, tr("°"),
                     tr("The way every gradient runs: 0° left to right, 90° top to bottom. "
                        "The light comes from where it starts."));
    m_brightness = addRow(grid, tr("Brightness"), -100, 100, tr("%"),
                          tr("Lighter for a face turned to the light, darker for one turned away"));
    m_variation = addRow(grid, tr("Variation"), 0, 100, tr("%"),
                         tr("How much one area differs in tone from the next, so neighbours stay distinct"));

    m_minArea = new QSpinBox(this);
    m_minArea->setRange(1, 100000);
    m_minArea->setSuffix(tr(" px"));
    m_minArea->setKeyboardTracking(false);
    m_minArea->setToolTip(tr("Areas smaller than this are left as they are (rivets, specks, the gaps in lettering)"));
    grid->addWidget(new QLabel(tr("Skip areas under"), this), grid->rowCount(), 0);
    grid->addWidget(m_minArea, grid->rowCount() - 1, 1, 1, 2);

    m_whiteOnly = new QCheckBox(tr("Only areas that are still white"), this);
    m_whiteOnly->setToolTip(tr("Leaves areas you've already coloured alone. Unticked, they're shaded too: "
                               "the shading darkens the colour under it."));
    grid->addWidget(m_whiteOnly, grid->rowCount(), 1, 1, 2);

    m_preview = new QCheckBox(tr("Preview"), this);
    m_preview->setChecked(true);
    connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            emit previewRequested(m_settings, m_options);
        else
            emit previewCleared();
    });

    auto *hint = new QLabel(tr("Shades every enclosed area of the layer, or those mostly inside the selection, "
                               "on a Multiply layer above it."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addLayout(grid);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);
    setMinimumWidth(400);

    m_syncing = true;
    m_angle.spin->setValue(int(std::lround(m_settings.angle)) % 360);
    m_brightness.spin->setValue(int(std::lround(m_settings.brightness * 100.0)));
    m_variation.spin->setValue(int(std::lround(m_settings.variation * 100.0)));
    m_minArea->setValue(m_options.minArea);
    m_whiteOnly->setChecked(m_options.whiteOnly);
    m_syncing = false;

    const auto touch = [this] {
        if (!m_syncing)
            changed();
    };
    connect(m_gradient, &QComboBox::currentIndexChanged, this, touch);
    connect(m_tint, &QCheckBox::toggled, this, touch);
    connect(m_minArea, &QSpinBox::valueChanged, this, touch);
    connect(m_whiteOnly, &QCheckBox::toggled, this, touch);
    changed();
    m_timer->stop(); // the window shows the first preview itself
}

ShadeDialog::Row ShadeDialog::addRow(QGridLayout *grid, const QString &label, int min, int max, const QString &suffix,
                                     const QString &tip)
{
    const int row = grid->rowCount();
    Row r;
    r.slider = new QSlider(Qt::Horizontal, this);
    r.slider->setRange(min, max);
    r.spin = new QSpinBox(this);
    r.spin->setRange(min, max);
    r.spin->setSuffix(suffix);
    r.spin->setKeyboardTracking(false);
    auto *name = new QLabel(label, this);
    name->setToolTip(tip);
    r.slider->setToolTip(tip);
    r.spin->setToolTip(tip);
    grid->addWidget(name, row, 0);
    grid->addWidget(r.slider, row, 1);
    grid->addWidget(r.spin, row, 2);
    QSlider *slider = r.slider;
    QSpinBox *spin = r.spin;
    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, &QSpinBox::valueChanged, this, [this, slider](int v) {
        {
            const QSignalBlocker block(slider);
            slider->setValue(v);
        }
        if (!m_syncing)
            changed();
    });
    return r;
}

int ShadeDialog::gradientIndex() const
{
    return m_gradient->currentIndex();
}

bool ShadeDialog::previewEnabled() const
{
    return m_preview->isChecked();
}

void ShadeDialog::changed()
{
    m_settings.stops = gradientAt(m_gradient->currentIndex());
    m_settings.tint = m_tint->isChecked() ? m_tintColour : QColor();
    m_settings.angle = m_angle.spin->value();
    m_settings.brightness = m_brightness.spin->value() / 100.0;
    m_settings.variation = m_variation.spin->value() / 100.0;
    m_settings = m_settings.normalized();
    m_options.minArea = m_minArea->value();
    m_options.whiteOnly = m_whiteOnly->isChecked();
    m_timer->start();
}
