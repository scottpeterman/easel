#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QToolButton;

// Current painting color: a swatch that opens the color picker, plus its hex value.
class ColorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPanel(QWidget *parent = nullptr);

    QColor color() const { return m_color; }

public slots:
    void setColor(const QColor &color);

signals:
    void colorChanged(const QColor &color);

private:
    void pickColor();

    QColor m_color = Qt::black;
    QToolButton *m_swatch = nullptr;
    QLabel *m_hex = nullptr;
};
