#pragma once

#include "textrender.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QToolButton;

// The little window the Text tool types into: the words, and the font (any
// installed on this machine), size, bold, italic, alignment and smoothing.
// It stays open beside the canvas, which shows the text as you type; OK (or
// Ctrl+Enter) places it, Cancel or closing the window drops it.
class TextPanel : public QDialog
{
    Q_OBJECT

public:
    explicit TextPanel(QWidget *parent = nullptr);

    // Everything but the colour, which the window supplies.
    easeletch::TextSettings settings() const;
    // Fills the panel from text already placed, to change it.
    void setSettings(const easeletch::TextSettings &settings);
    QString text() const;
    void setText(const QString &text);
    void setFontFamily(const QString &family);
    void setPixelSize(int size);
    void setBold(bool on);
    void setItalic(bool on);
    void setAlignment(Qt::Alignment align);
    void setSmooth(bool on);
    // The letters' colour, as the button shows it. It's the painting colour:
    // the window keeps this and the Color panel the same.
    void setColor(const QColor &color);
    QColor color() const { return m_color; }
    // A line round the letters, in pixels (0 = none), and its colour.
    void setOutline(int width);
    void setOutlineColor(const QColor &color);
    // The width the words wrap inside (0 = no wrapping).
    void setBoxWidth(int width);
    // What the button that places the text says: "Place", or "Update".
    void setEditing(bool editing);
    // Puts the cursor in the text box.
    void focusText();

    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

signals:
    // The text or how it's drawn changed.
    void changed();
    // A colour for the letters was chosen with the panel's own button.
    void colorPicked(const QColor &color);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPlainTextEdit *m_edit = nullptr;
    QFontComboBox *m_font = nullptr;
    QSpinBox *m_size = nullptr;
    QToolButton *m_bold = nullptr;
    QToolButton *m_italic = nullptr;
    QComboBox *m_align = nullptr;
    QCheckBox *m_smooth = nullptr;
    QToolButton *m_colorButton = nullptr;
    QColor m_color = Qt::black;
    QSpinBox *m_outline = nullptr;
    QToolButton *m_outlineColorButton = nullptr;
    QColor m_outlineColor = Qt::black;
    QSpinBox *m_box = nullptr;
    QPushButton *m_place = nullptr;
};
