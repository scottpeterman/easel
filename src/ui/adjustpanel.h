#pragma once

#include "adjust.h"

#include <QList>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QSlider;
class QStackedWidget;
class QToolButton;

// A tone curve to shape by hand: input along the bottom, output up the side.
// Click the line to add a point, drag a point to move it, drag it off the
// square (or press Delete) to remove it. Two points always stay.
class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveWidget(QWidget *parent = nullptr);

    QList<QPointF> points() const { return m_points; }
    void setPoints(const QList<QPointF> &points);
    int selected() const { return m_selected; }

    // The square the curve is drawn in, in widget coordinates.
    QRectF plotRect() const;
    QSize sizeHint() const override { return QSize(220, 220); }
    QSize minimumSizeHint() const override { return QSize(140, 140); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    // The user changed the curve (not setPoints()).
    void pointsChanged(const QList<QPointF> &points);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointF toWidget(const QPointF &p) const;
    QPointF fromWidget(const QPointF &pos) const;
    int pointAt(const QPointF &pos) const;
    void removeSelected();

    QList<QPointF> m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    int m_selected = -1;
    bool m_dragging = false;
};

// The Adjustment panel: the settings of the active layer when it's an
// adjustment layer, changed live; otherwise buttons to add one.
//
// The panel only reports what the user asked for; the window changes the
// document and calls setAdjustment() again.
class AdjustPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AdjustPanel(QWidget *parent = nullptr);

    // Shows a layer's settings; type None shows the buttons that add a layer.
    void setAdjustment(const easeletch::Adjustment &adjustment);
    easeletch::Adjustment adjustment() const { return m_adjust; }
    CurveWidget *curveWidget() const { return m_curve; }
    static QString typeName(easeletch::AdjustmentType type);

signals:
    void adjustmentChanged(const easeletch::Adjustment &adjustment);
    void addRequested(easeletch::AdjustmentType type);

private:
    struct Row {
        QSlider *slider = nullptr;
        QDoubleSpinBox *spin = nullptr;
        double easeletch::Adjustment::*field = nullptr;
        double scale = 1.0; // shown value = stored value * scale
    };

    QWidget *makePage(easeletch::AdjustmentType type);
    void addRow(class QGridLayout *grid, const QString &label, double easeletch::Adjustment::*field, double min,
                double max, double scale, int decimals, const QString &suffix, const QString &tip = {});
    void syncRows();
    void changed();

    easeletch::Adjustment m_adjust;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_title = nullptr;
    QToolButton *m_reset = nullptr;
    CurveWidget *m_curve = nullptr;
    QList<Row> m_rows;
    bool m_syncing = false;
};
