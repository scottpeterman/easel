#include "colorpanel.h"

#include "colorwheel.h"

#include <QAbstractButton>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

const char *const kDefaultPalette[] = {
    "#000000", "#404040", "#808080", "#c0c0c0", "#ffffff", "#d32f2f", "#f57c00", "#fbc02d",
    "#388e3c", "#00897b", "#1976d2", "#7b1fa2", "#c2185b", "#795548", "#ffccbc", "#b3e5fc",
};

// Opaque, 8-bit sRGB. Colours built from floats (e.g. the eyedropper) carry
// extra precision that makes QColor == fail on colours that look identical,
// which would break de-duplication in Recent and the palette.
QColor opaque(const QColor &c)
{
    const QColor rgb = c.toRgb();
    return QColor(rgb.red(), rgb.green(), rgb.blue());
}

QString hexOf(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}

void clearLayout(QLayout *layout)
{
    // Deferred: the swatch being cleared may be the one whose click or context
    // menu triggered the rebuild, and is still on the call stack.
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

} // namespace

// A clickable colour square.
class SwatchButton : public QAbstractButton
{
public:
    SwatchButton(const QColor &color, QSize size, QWidget *parent)
        : QAbstractButton(parent)
        , m_color(color)
        , m_size(size)
    {
        setFixedSize(size);
        setToolTip(hexOf(color));
        setCursor(Qt::PointingHandCursor);
    }

    void setColor(const QColor &c)
    {
        m_color = c;
        setToolTip(hexOf(c));
        update();
    }
    QColor color() const { return m_color; }

    std::function<void(const QPoint &)> onContextMenu;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.fillRect(r, m_color);
        p.setPen(underMouse() ? palette().color(QPalette::Highlight) : QColor(0, 0, 0, 90));
        p.drawRect(r);
    }
    void enterEvent(QEnterEvent *e) override
    {
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        update();
        QAbstractButton::leaveEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        if (onContextMenu)
            onContextMenu(e->globalPos());
    }
    QSize sizeHint() const override { return m_size; }

private:
    QColor m_color;
    QSize m_size;
};

ColorPanel::ColorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_wheel = new ColorWheel(this);
    layout->addWidget(m_wheel);
    connect(m_wheel, &ColorWheel::colorChanged, this, &ColorPanel::setColor);

    auto *row = new QHBoxLayout;
    m_current = new SwatchButton(m_color, QSize(44, 24), this);
    m_current->setCursor(Qt::ArrowCursor);
    m_hex = new QLineEdit(this);
    m_hex->setMaxLength(7);
    m_hex->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), m_hex));
    m_hex->setToolTip(tr("Hex colour, e.g. #1E88E5"));
    connect(m_hex, &QLineEdit::editingFinished, this, &ColorPanel::applyHex);
    row->addWidget(m_current);
    row->addWidget(m_hex, 1);
    layout->addLayout(row);

    auto *recentLabel = new QLabel(tr("Recent"), this);
    recentLabel->setEnabled(false);
    layout->addWidget(recentLabel);
    m_recentRow = new QHBoxLayout;
    m_recentRow->setSpacing(3);
    layout->addLayout(m_recentRow);

    auto *paletteLabel = new QLabel(tr("Palette"), this);
    paletteLabel->setEnabled(false);
    layout->addWidget(paletteLabel);
    m_paletteGrid = new QGridLayout;
    m_paletteGrid->setSpacing(3);
    layout->addLayout(m_paletteGrid);
    layout->addStretch(1);

    for (const char *hex : kDefaultPalette)
        m_saved.append(QColor(QString::fromLatin1(hex)));

    setColor(m_color);
    rebuildRecent();
    rebuildPalette();
}

void ColorPanel::setColor(const QColor &color)
{
    if (!color.isValid())
        return;
    const QColor c = opaque(color);
    const bool changed = c != m_color;
    m_color = c;

    // Keep the wheel's hue when the change came from the wheel itself.
    if (sender() != m_wheel)
        m_wheel->setColor(c);
    m_current->setColor(c);
    if (!m_hex->hasFocus())
        m_hex->setText(hexOf(c));

    if (changed)
        emit colorChanged(c);
}

void ColorPanel::applyHex()
{
    QString text = m_hex->text().trimmed();
    if (!text.startsWith(QLatin1Char('#')))
        text.prepend(QLatin1Char('#'));
    const QColor c(text);
    if (text.size() == 7 && c.isValid())
        setColor(c);
    m_hex->setText(hexOf(m_color));
}

void ColorPanel::noteUsed(const QColor &color)
{
    const QColor c = opaque(color);
    m_recent.removeAll(c);
    m_recent.prepend(c);
    while (m_recent.size() > MaxRecent)
        m_recent.removeLast();
    rebuildRecent();
}

void ColorPanel::addToPalette(const QColor &color)
{
    const QColor c = opaque(color);
    if (m_saved.contains(c))
        return;
    m_saved.append(c);
    rebuildPalette();
}

void ColorPanel::replaceInPalette(int index, const QColor &color)
{
    if (index < 0 || index >= m_saved.size())
        return;
    m_saved[index] = opaque(color);
    rebuildPalette();
}

void ColorPanel::removeFromPalette(int index)
{
    if (index < 0 || index >= m_saved.size())
        return;
    m_saved.removeAt(index);
    rebuildPalette();
}

void ColorPanel::setSavedColors(const QList<QColor> &colors)
{
    m_saved.clear();
    for (const QColor &c : colors)
        if (c.isValid() && !m_saved.contains(opaque(c)))
            m_saved.append(opaque(c));
    rebuildPalette();
}

void ColorPanel::rebuildRecent()
{
    clearLayout(m_recentRow);
    for (const QColor &c : std::as_const(m_recent)) {
        auto *s = new SwatchButton(c, QSize(18, 18), this);
        connect(s, &QAbstractButton::clicked, this, [this, c] { setColor(c); });
        m_recentRow->addWidget(s);
    }
    m_recentRow->addStretch(1);
}

void ColorPanel::rebuildPalette()
{
    clearLayout(m_paletteGrid);
    int i = 0;
    for (; i < m_saved.size(); ++i) {
        const QColor c = m_saved.at(i);
        auto *s = new SwatchButton(c, QSize(22, 22), this);
        s->setToolTip(tr("%1 — right-click to replace or remove").arg(hexOf(c)));
        connect(s, &QAbstractButton::clicked, this, [this, c] { setColor(c); });
        s->onContextMenu = [this, i](const QPoint &pos) { showPaletteMenu(i, pos); };
        m_paletteGrid->addWidget(s, i / PaletteColumns, i % PaletteColumns);
    }
    auto *add = new QToolButton(this);
    add->setText(QStringLiteral("+"));
    add->setFixedSize(22, 22);
    add->setToolTip(tr("Add the current colour"));
    connect(add, &QToolButton::clicked, this, [this] { addToPalette(m_color); });
    m_paletteGrid->addWidget(add, i / PaletteColumns, i % PaletteColumns);
}

void ColorPanel::showPaletteMenu(int index, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Replace with Current Colour"), this, [this, index] { replaceInPalette(index, m_color); });
    menu.addAction(tr("Remove"), this, [this, index] { removeFromPalette(index); });
    menu.exec(globalPos);
}

void ColorPanel::loadSettings(QSettings &s)
{
    const auto toColors = [](const QStringList &names) {
        QList<QColor> out;
        for (const QString &n : names)
            if (QColor c(n); c.isValid())
                out.append(c);
        return out;
    };
    if (s.contains(QStringLiteral("color/palette")))
        setSavedColors(toColors(s.value(QStringLiteral("color/palette")).toStringList()));
    m_recent = toColors(s.value(QStringLiteral("color/recent")).toStringList()).mid(0, MaxRecent);
    rebuildRecent();
    setColor(s.value(QStringLiteral("color/current"), QColor(Qt::black)).value<QColor>());
}

void ColorPanel::saveSettings(QSettings &s) const
{
    QStringList palette, recent;
    for (const QColor &c : m_saved)
        palette << hexOf(c);
    for (const QColor &c : m_recent)
        recent << hexOf(c);
    s.setValue(QStringLiteral("color/palette"), palette);
    s.setValue(QStringLiteral("color/recent"), recent);
    s.setValue(QStringLiteral("color/current"), m_color);
}
