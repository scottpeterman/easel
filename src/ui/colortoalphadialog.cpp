#include "colortoalphadialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

ColorToAlphaDialog::ColorToAlphaDialog(const QColor &initial, const QColor &corner, double threshold,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Color to Alpha"));

    m_swatch = new QPushButton(this);
    m_swatch->setFixedSize(56, 24);
    m_swatch->setToolTip(tr("Choose the colour to make transparent"));
    connect(m_swatch, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Colour to Remove"));
        if (c.isValid())
            setColor(c);
    });
    auto *cornerButton = new QPushButton(tr("Top-left pixel"), this);
    cornerButton->setToolTip(tr("Use the colour of the canvas's top-left pixel"));
    cornerButton->setEnabled(corner.isValid());
    connect(cornerButton, &QPushButton::clicked, this, [this, corner] { setColor(corner); });
    auto *colorRow = new QWidget(this);
    auto *row = new QHBoxLayout(colorRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_swatch);
    row->addWidget(cornerButton);
    row->addStretch(1);

    m_threshold = new QSpinBox(this);
    m_threshold->setRange(0, 50);
    m_threshold->setSuffix(tr("%"));
    m_threshold->setValue(int(std::lround(threshold * 100.0)));
    m_threshold->setToolTip(tr("Pixels this close to the colour become fully transparent "
                               "(clears noise in a background that isn't quite flat)"));

    auto *form = new QFormLayout;
    form->addRow(tr("Colour"), colorRow);
    form->addRow(tr("Threshold"), m_threshold);

    auto *note = new QLabel(tr("Applies to the selection, or the whole canvas without one."), this);
    note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);

    setColor(initial);
}

void ColorToAlphaDialog::setColor(const QColor &c)
{
    m_color = QColor(c.red(), c.green(), c.blue());
    m_swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid palette(mid);").arg(m_color.name()));
    m_swatch->setText(QString());
}

double ColorToAlphaDialog::threshold() const
{
    return m_threshold->value() / 100.0;
}
