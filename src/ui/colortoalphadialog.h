#pragma once

#include <QColor>
#include <QDialog>

class QPushButton;
class QSpinBox;

// Color to Alpha: which colour to take out, and how faint a leftover has to
// be to go fully transparent.
class ColorToAlphaDialog : public QDialog
{
    Q_OBJECT

public:
    // corner: the canvas's top-left pixel, offered as a one-click choice
    // (sprite sheets usually have the background there).
    ColorToAlphaDialog(const QColor &initial, const QColor &corner, double threshold, QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    double threshold() const;

private:
    void setColor(const QColor &c);

    QColor m_color;
    QPushButton *m_swatch = nullptr;
    QSpinBox *m_threshold = nullptr;
};
