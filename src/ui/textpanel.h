#pragma once

#include "textrender.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPlainTextEdit;
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
    QString text() const;
    void setText(const QString &text);
    void setFontFamily(const QString &family);
    void setPixelSize(int size);
    void setBold(bool on);
    void setItalic(bool on);
    void setAlignment(Qt::Alignment align);
    void setSmooth(bool on);
    // Puts the cursor in the text box.
    void focusText();

    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

signals:
    // The text or how it's drawn changed.
    void changed();

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
};
