#include "textpanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

using easeletch::TextSettings;

TextPanel::TextPanel(QWidget *parent)
    : QDialog(parent, Qt::Tool)
{
    setWindowTitle(tr("Text"));
    setModal(false);

    auto *column = new QVBoxLayout(this);
    column->setSpacing(6);

    auto *row = new QHBoxLayout;
    m_font = new QFontComboBox(this);
    m_font->setToolTip(tr("Every font installed on this computer"));
    m_size = new QSpinBox(this);
    m_size->setRange(TextSettings::MinSize, TextSettings::MaxSize);
    m_size->setValue(48);
    m_size->setSuffix(tr(" px"));
    m_size->setToolTip(tr("Letter height in canvas pixels"));
    row->addWidget(m_font, 1);
    row->addWidget(m_size);
    column->addLayout(row);

    row = new QHBoxLayout;
    m_bold = new QToolButton(this);
    m_bold->setText(tr("Bold"));
    m_bold->setCheckable(true);
    m_italic = new QToolButton(this);
    m_italic->setText(tr("Italic"));
    m_italic->setCheckable(true);
    m_align = new QComboBox(this);
    m_align->addItem(tr("Left"), int(Qt::AlignLeft));
    m_align->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Right"), int(Qt::AlignRight));
    m_align->setToolTip(tr("How the lines sit against each other, and against the point you clicked"));
    m_smooth = new QCheckBox(tr("Smooth"), this);
    m_smooth->setChecked(true);
    m_smooth->setToolTip(tr("Soft letter edges. Untick for hard pixels (sprites, pixel art)."));
    row->addWidget(m_bold);
    row->addWidget(m_italic);
    row->addWidget(m_align);
    row->addWidget(m_smooth);
    row->addStretch(1);
    column->addLayout(row);

    row = new QHBoxLayout;
    m_outline = new QSpinBox(this);
    m_outline->setRange(0, TextSettings::MaxOutline);
    m_outline->setSpecialValueText(tr("None"));
    m_outline->setSuffix(tr(" px"));
    m_outline->setToolTip(tr("A line round the letters, so they read on any picture"));
    m_outlineColorButton = new QToolButton(this);
    m_outlineColorButton->setToolTip(tr("The outline's colour"));
    m_outlineColorButton->setFixedWidth(36);
    connect(m_outlineColorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_outlineColor, this, tr("Outline colour"));
        if (c.isValid())
            setOutlineColor(c);
    });
    setOutlineColor(m_outlineColor);
    m_box = new QSpinBox(this);
    m_box->setRange(0, TextSettings::MaxBoxWidth);
    m_box->setSingleStep(10);
    m_box->setSpecialValueText(tr("Off"));
    m_box->setSuffix(tr(" px"));
    m_box->setToolTip(tr("Wrap the words inside this width. Off: lines break only where you press Enter."));
    row->addWidget(new QLabel(tr("Outline"), this));
    row->addWidget(m_outline);
    row->addWidget(m_outlineColorButton);
    row->addSpacing(12);
    row->addWidget(new QLabel(tr("Wrap at"), this));
    row->addWidget(m_box);
    row->addStretch(1);
    column->addLayout(row);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(tr("Type here. The canvas shows it as you go."));
    m_edit->setTabChangesFocus(true);
    m_edit->setMinimumSize(320, 110);
    column->addWidget(m_edit, 1);

    auto *hint = new QLabel(tr("Colour comes from the Color panel. Drag on the canvas to move the text."), this);
    hint->setEnabled(false);
    hint->setWordWrap(true);
    column->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_place = buttons->button(QDialogButtonBox::Ok);
    setEditing(false);
    // Enter belongs to the text box: it starts a new line.
    buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Ok)->setDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    // Ctrl+Enter places the text from anywhere in the panel, the text box included.
    for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *place = new QShortcut(QKeySequence(Qt::CTRL | key), this);
        place->setContext(Qt::WidgetWithChildrenShortcut);
        connect(place, &QShortcut::activated, this, &QDialog::accept);
    }

    connect(m_edit, &QPlainTextEdit::textChanged, this, &TextPanel::changed);
    connect(m_font, &QFontComboBox::currentFontChanged, this, &TextPanel::changed);
    connect(m_size, &QSpinBox::valueChanged, this, &TextPanel::changed);
    connect(m_bold, &QToolButton::toggled, this, &TextPanel::changed);
    connect(m_italic, &QToolButton::toggled, this, &TextPanel::changed);
    connect(m_align, &QComboBox::currentIndexChanged, this, &TextPanel::changed);
    connect(m_smooth, &QCheckBox::toggled, this, &TextPanel::changed);
    connect(m_outline, &QSpinBox::valueChanged, this, &TextPanel::changed);
    connect(m_box, &QSpinBox::valueChanged, this, &TextPanel::changed);
}

TextSettings TextPanel::settings() const
{
    TextSettings s;
    s.text = m_edit->toPlainText();
    s.family = m_font->currentFont().family();
    s.pixelSize = m_size->value();
    s.bold = m_bold->isChecked();
    s.italic = m_italic->isChecked();
    s.align = Qt::Alignment(m_align->currentData().toInt());
    s.smooth = m_smooth->isChecked();
    s.outline = m_outline->value();
    s.outlineColor = m_outlineColor;
    s.boxWidth = m_box->value();
    return s;
}

void TextPanel::setSettings(const TextSettings &s)
{
    setText(s.text);
    if (!s.family.isEmpty())
        setFontFamily(s.family);
    setPixelSize(s.pixelSize);
    setBold(s.bold);
    setItalic(s.italic);
    setAlignment(s.align);
    setSmooth(s.smooth);
    setOutline(s.outline);
    setOutlineColor(s.outlineColor);
    setBoxWidth(s.boxWidth);
}

QString TextPanel::text() const
{
    return m_edit->toPlainText();
}

void TextPanel::setText(const QString &text)
{
    m_edit->setPlainText(text);
}

void TextPanel::setFontFamily(const QString &family)
{
    m_font->setCurrentFont(QFont(family));
}

void TextPanel::setPixelSize(int size)
{
    m_size->setValue(size);
}

void TextPanel::setBold(bool on)
{
    m_bold->setChecked(on);
}

void TextPanel::setItalic(bool on)
{
    m_italic->setChecked(on);
}

void TextPanel::setAlignment(Qt::Alignment align)
{
    const int i = m_align->findData(int(align));
    m_align->setCurrentIndex(i < 0 ? 0 : i);
}

void TextPanel::setSmooth(bool on)
{
    m_smooth->setChecked(on);
}

void TextPanel::setOutline(int width)
{
    m_outline->setValue(width);
}

void TextPanel::setOutlineColor(const QColor &color)
{
    const bool same = color == m_outlineColor;
    m_outlineColor = color;
    m_outlineColorButton->setStyleSheet(
        QStringLiteral("QToolButton { background: %1; border: 1px solid palette(mid); }").arg(color.name()));
    if (!same)
        emit changed();
}

void TextPanel::setBoxWidth(int width)
{
    m_box->setValue(width);
}

void TextPanel::setEditing(bool editing)
{
    m_place->setText(editing ? tr("Update") : tr("Place"));
    m_place->setToolTip(editing ? tr("Change the text on its layer (Ctrl+Enter)")
                                : tr("Put the text on its own new layer (Ctrl+Enter)"));
}

void TextPanel::focusText()
{
    m_edit->setFocus();
}

void TextPanel::loadSettings(QSettings &s)
{
    s.beginGroup(QStringLiteral("text"));
    const QString family = s.value(QStringLiteral("family")).toString();
    if (!family.isEmpty())
        setFontFamily(family);
    setPixelSize(s.value(QStringLiteral("size"), m_size->value()).toInt());
    setBold(s.value(QStringLiteral("bold"), false).toBool());
    setItalic(s.value(QStringLiteral("italic"), false).toBool());
    setAlignment(Qt::Alignment(s.value(QStringLiteral("align"), int(Qt::AlignLeft)).toInt()));
    setSmooth(s.value(QStringLiteral("smooth"), true).toBool());
    setOutline(s.value(QStringLiteral("outline"), 0).toInt());
    if (const QColor c = QColor::fromString(s.value(QStringLiteral("outlineColor")).toString()); c.isValid())
        setOutlineColor(c);
    s.endGroup();
}

void TextPanel::saveSettings(QSettings &s) const
{
    const TextSettings t = settings();
    s.beginGroup(QStringLiteral("text"));
    s.setValue(QStringLiteral("family"), t.family);
    s.setValue(QStringLiteral("size"), t.pixelSize);
    s.setValue(QStringLiteral("bold"), t.bold);
    s.setValue(QStringLiteral("italic"), t.italic);
    s.setValue(QStringLiteral("align"), int(t.align));
    s.setValue(QStringLiteral("smooth"), t.smooth);
    s.setValue(QStringLiteral("outline"), t.outline);
    s.setValue(QStringLiteral("outlineColor"), t.outlineColor.name());
    s.endGroup();
}

void TextPanel::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ControlModifier)) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
