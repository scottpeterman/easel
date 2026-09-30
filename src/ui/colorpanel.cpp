#include "colorpanel.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

namespace {

QIcon swatchIcon(const QColor &color, const QSize &size)
{
    QPixmap pm(size);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.fillRect(0, 0, size.width() / 2, size.height() / 2, QColor(0xcc, 0xcc, 0xcc));
    p.fillRect(size.width() / 2, size.height() / 2, size.width() / 2, size.height() / 2,
               QColor(0xcc, 0xcc, 0xcc));
    p.fillRect(pm.rect(), color);
    p.setPen(QColor(0x1e, 0x1e, 0x1e));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

} // namespace

ColorPanel::ColorPanel(QWidget *parent)
    : QWidget(parent)
{
    m_swatch = new QToolButton(this);
    m_swatch->setIconSize(QSize(40, 40));
    m_swatch->setAutoRaise(true);
    m_swatch->setToolTip(tr("Choose color"));
    connect(m_swatch, &QToolButton::clicked, this, &ColorPanel::pickColor);

    m_hex = new QLabel(this);
    m_hex->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_swatch);
    layout->addWidget(m_hex, 1);

    setColor(m_color);
}

void ColorPanel::setColor(const QColor &color)
{
    if (!color.isValid())
        return;
    const bool changed = color != m_color;
    m_color = color;
    m_swatch->setIcon(swatchIcon(m_color, m_swatch->iconSize()));
    m_hex->setText(m_color.alpha() == 255 ? m_color.name(QColor::HexRgb).toUpper()
                                          : m_color.name(QColor::HexArgb).toUpper());
    if (changed)
        emit colorChanged(m_color);
}

void ColorPanel::pickColor()
{
    const QColor c = QColorDialog::getColor(m_color, this, tr("Color"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid())
        setColor(c);
}
