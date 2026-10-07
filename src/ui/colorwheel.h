#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

// Hue ring with a saturation/value square inside it.
//
// Hue is kept separately from the colour, so dragging saturation or value to a
// grey and back doesn't lose the hue you were on.
class ColorWheel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorWheel(QWidget *parent = nullptr);

    QColor color() const;
    double hue() const { return m_h; }
    double saturation() const { return m_s; }
    double value() const { return m_v; }

    QSize sizeHint() const override { return QSize(220, 220); }
    // No height-for-width: a dock gives its panel what height there is, and a
    // wheel that insisted on being as tall as it is wide pushed everything
    // under it out of the panel on a short screen. It draws in the largest
    // square that fits instead.
    QSize minimumSizeHint() const override { return QSize(MinimumSide, MinimumSide); }
    static constexpr int MinimumSide = 120;

    // Geometry, in widget coordinates, for tests and hit-testing.
    QPointF center() const;
    double outerRadius() const;
    double innerRadius() const;
    QRectF squareRect() const;

public slots:
    void setColor(const QColor &color);

signals:
    // Emitted when the user changes the colour, not for setColor().
    void colorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class Drag { None, Ring, Square };

    void dragTo(const QPointF &pos);
    const QImage &ringImage(qreal dpr);
    const QImage &squareImage(qreal dpr);

    double m_h = 0.0; // 0..1
    double m_s = 0.0;
    double m_v = 0.0;
    Drag m_drag = Drag::None;

    QImage m_ring;
    QImage m_square;
    double m_squareHue = -1.0;
};
