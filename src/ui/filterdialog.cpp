#include "filterdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using easeletch::Filter;
using easeletch::FilterType;

namespace {
constexpr int kSliderSteps = 1000;
}

QString FilterDialog::typeName(FilterType type)
{
    switch (type) {
    case FilterType::GaussianBlur: return tr("Gaussian Blur");
    case FilterType::Sharpen: return tr("Sharpen");
    case FilterType::Noise: return tr("Add Noise");
    case FilterType::Pixelate: return tr("Pixelate");
    case FilterType::Despeckle: return tr("Despeckle");
    case FilterType::PencilSketch: return tr("Pencil Sketch");
    case FilterType::InkSketch: return tr("Ink Sketch");
    }
    return QString();
}

FilterDialog::FilterDialog(const Filter &filter, QWidget *parent)
    : QDialog(parent)
    , m_filter(filter.normalized())
{
    setWindowTitle(typeName(m_filter.type));
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_preview->isChecked())
            emit previewRequested(m_filter);
    });

    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    auto *host = new QWidget(this);
    host->setLayout(grid);
    grid->setContentsMargins(0, 0, 0, 0);

    switch (m_filter.type) {
    case FilterType::GaussianBlur:
        // The slider is finer at the small radii, where a step shows most.
        m_radius = addRow(grid, tr("Radius"), 0.1, 250.0, 1, tr(" px"), tr("How far the blur spreads"));
        break;
    case FilterType::Sharpen:
        m_amount = addRow(grid, tr("Amount"), 0.0, 500.0, 0, tr("%"), tr("How strongly edges are picked out"));
        m_radius = addRow(grid, tr("Radius"), 0.1, 50.0, 1, tr(" px"),
                          tr("The width of the edges: small for fine detail, large for overall punch"));
        break;
    case FilterType::Noise:
        m_amount = addRow(grid, tr("Amount"), 0.0, 100.0, 0, tr("%"), QString());
        m_monochrome = new QCheckBox(tr("Monochrome"), this);
        m_monochrome->setToolTip(tr("Grain, lighter and darker, with no coloured specks"));
        grid->addWidget(m_monochrome, grid->rowCount(), 1, 1, 2);
        connect(m_monochrome, &QCheckBox::toggled, this, [this](bool on) {
            if (m_syncing)
                return;
            m_filter.monochrome = on;
            changed();
        });
        break;
    case FilterType::Pixelate:
        m_cell = addRow(grid, tr("Block size"), 2.0, 256.0, 0, tr(" px"), QString());
        break;
    case FilterType::Despeckle:
        m_speck = addRow(grid, tr("Speck size"), 1.0, 5000.0, 0, tr(" px"),
                         tr("The largest speck removed, counted in pixels. Anything bigger is left exactly as it is"));
        m_tolerance = addRow(grid, tr("Tolerance"), 0.0, 100.0, 0, tr("%"),
                             tr("How alike neighbouring pixels must be to count as one area of colour. "
                                "Raise it on grainy paper; lower it if faint detail starts to go"));
        break;
    case FilterType::PencilSketch:
        m_radius = addRow(grid, tr("Softness"), 1.0, 100.0, 1, tr(" px"),
                          tr("Small for thin outlines only; large for broad, soft shading as well"));
        m_darkness = addRow(grid, tr("Darkness"), 50.0, 400.0, 0, tr("%"), tr("How heavy the pencil is"));
        break;
    case FilterType::InkSketch:
        m_ink = addRow(grid, tr("Ink"), 0.0, 100.0, 0, tr("%"),
                       tr("How much of the picture goes to solid black. Low gives outlines on white; "
                          "raise it to fill the shadows"));
        m_radius = addRow(grid, tr("Line width"), 0.3, 10.0, 1, tr(" px"), QString());
        m_detail = addRow(grid, tr("Detail"), 1.0, 60.0, 0, QString(),
                          tr("How hard edges are pushed: low keeps the main outlines, high picks up fine texture"));
        m_hardness = addRow(grid, tr("Hardness"), 1.0, 100.0, 0, QString(),
                            tr("Low is a soft wash; high is a hard pen line"));
        break;
    }

    m_preview = new QCheckBox(tr("Preview"), this);
    m_preview->setChecked(true);
    m_preview->setToolTip(tr("Show the result on the canvas as you change the settings"));
    connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            emit previewRequested(m_filter);
        else
            emit previewCleared();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(host);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);
    setMinimumWidth(380);
    setFilter(m_filter);
}

FilterDialog::Row FilterDialog::addRow(QGridLayout *grid, const QString &label, double min, double max, int decimals,
                                       const QString &suffix, const QString &tip)
{
    const int row = grid->rowCount();
    Row r;
    r.slider = new QSlider(Qt::Horizontal, this);
    r.slider->setRange(0, kSliderSteps);
    r.spin = new QDoubleSpinBox(this);
    r.spin->setRange(min, max);
    r.spin->setDecimals(decimals);
    r.spin->setSuffix(suffix);
    r.spin->setKeyboardTracking(false);
    auto *name = new QLabel(label, this);
    if (!tip.isEmpty()) {
        name->setToolTip(tip);
        r.slider->setToolTip(tip);
        r.spin->setToolTip(tip);
    }
    grid->addWidget(name, row, 0);
    grid->addWidget(r.slider, row, 1);
    grid->addWidget(r.spin, row, 2);

    // The slider runs on a curve: half its travel covers the first eighth of
    // the range, where small values matter most.
    const auto toValue = [min, max](int v) { return min + (max - min) * std::pow(v / double(kSliderSteps), 3.0); };
    const auto toSlider = [min, max](double v) {
        return int(std::lround(std::cbrt(std::clamp((v - min) / (max - min), 0.0, 1.0)) * kSliderSteps));
    };
    QSlider *slider = r.slider;
    QDoubleSpinBox *spin = r.spin;
    connect(slider, &QSlider::valueChanged, this, [this, spin, toValue](int v) {
        if (!m_syncing)
            spin->setValue(toValue(v));
    });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, toSlider](double v) {
        {
            const QSignalBlocker block(slider);
            slider->setValue(toSlider(v));
        }
        if (!m_syncing)
            changed();
    });
    return r;
}

bool FilterDialog::previewEnabled() const
{
    return m_preview->isChecked();
}

void FilterDialog::setFilter(const Filter &filter)
{
    m_filter = filter.normalized();
    m_syncing = true;
    const bool percent = m_filter.type == FilterType::Sharpen || m_filter.type == FilterType::Noise;
    if (m_radius.spin)
        m_radius.spin->setValue(m_filter.radius);
    if (m_amount.spin)
        m_amount.spin->setValue(m_filter.amount * (percent ? 100.0 : 1.0));
    if (m_cell.spin)
        m_cell.spin->setValue(m_filter.cell);
    if (m_speck.spin)
        m_speck.spin->setValue(m_filter.speck);
    if (m_tolerance.spin)
        m_tolerance.spin->setValue(m_filter.tolerance * 100.0);
    if (m_darkness.spin)
        m_darkness.spin->setValue(m_filter.darkness * 100.0);
    if (m_detail.spin)
        m_detail.spin->setValue(m_filter.detail);
    if (m_ink.spin)
        m_ink.spin->setValue(m_filter.ink * 100.0);
    if (m_hardness.spin)
        m_hardness.spin->setValue(m_filter.hardness);
    if (m_monochrome)
        m_monochrome->setChecked(m_filter.monochrome);
    m_syncing = false;
}

void FilterDialog::changed()
{
    if (m_radius.spin)
        m_filter.radius = m_radius.spin->value();
    if (m_amount.spin)
        m_filter.amount = m_amount.spin->value() / 100.0;
    if (m_cell.spin)
        m_filter.cell = int(std::lround(m_cell.spin->value()));
    if (m_speck.spin)
        m_filter.speck = int(std::lround(m_speck.spin->value()));
    if (m_tolerance.spin)
        m_filter.tolerance = m_tolerance.spin->value() / 100.0;
    if (m_darkness.spin)
        m_filter.darkness = m_darkness.spin->value() / 100.0;
    if (m_detail.spin)
        m_filter.detail = m_detail.spin->value();
    if (m_ink.spin)
        m_filter.ink = m_ink.spin->value() / 100.0;
    if (m_hardness.spin)
        m_filter.hardness = m_hardness.spin->value();
    m_filter = m_filter.normalized();
    m_timer->start();
}
