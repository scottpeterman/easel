#pragma once

#include "fillops.h"

#include <QDialog>
#include <QPixmap>
#include <QWidget>

class ColorWheel;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

// A gradient drawn as a bar, with a marker under each stop. Click the bar to
// add a stop there, drag a marker to move it, Delete removes the selected one
// (two always stay).
class GradientBar : public QWidget
{
    Q_OBJECT

public:
    explicit GradientBar(QWidget *parent = nullptr);

    easeletch::GradientStops stops() const { return m_stops; }
    void setStops(const easeletch::GradientStops &stops);
    int selected() const { return m_selected; }
    void setSelected(int index);

    // Adds a stop with the colour the gradient has there. Returns its index.
    int addStop(double position);
    bool removeStop(int index);
    void setStopColor(int index, const QColor &color);
    // Returns the stop's index afterwards: moving past a neighbour reorders them.
    int setStopPosition(int index, double position);

    // The strip of the widget the gradient is drawn in.
    QRect barRect() const;
    QSize sizeHint() const override { return QSize(420, 64); }
    QSize minimumSizeHint() const override { return QSize(200, 64); }

    // The gradient as a small picture (over a checkerboard where it's
    // transparent), for lists and buttons.
    static QPixmap swatch(const easeletch::GradientStops &stops, const QSize &size);

signals:
    void stopsChanged();
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    double positionAt(int x) const;
    int xOf(double position) const;
    int stopAt(const QPoint &pos) const;

    easeletch::GradientStops m_stops;
    int m_selected = 0;
    bool m_dragging = false;
};

// Edits a gradient's stops: where each is, its colour and how opaque it is.
class GradientDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GradientDialog(const easeletch::GradientStops &stops, QWidget *parent = nullptr);

    easeletch::GradientStops stops() const;
    // Set when the dialog was closed with Save as Preset: the name given.
    QString presetName() const { return m_presetName; }
    GradientBar *bar() const { return m_bar; }

private:
    void syncFromSelection();
    void applyColor(const QColor &opaque);
    void saveAsPreset();

    GradientBar *m_bar = nullptr;
    ColorWheel *m_wheel = nullptr;
    QLineEdit *m_hex = nullptr;
    QSlider *m_opacity = nullptr;
    QSpinBox *m_opacitySpin = nullptr;
    QDoubleSpinBox *m_position = nullptr;
    QPushButton *m_delete = nullptr;
    QString m_presetName;
    bool m_syncing = false;
};
