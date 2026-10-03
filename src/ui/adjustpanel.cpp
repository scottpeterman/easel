#include "adjustpanel.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using easeletch::Adjustment;
using easeletch::AdjustmentType;

namespace {

constexpr double kGrab = 9.0;     // how near a point a press takes hold of it
constexpr double kMinGap = 0.004; // points keep distinct inputs
constexpr int kSliderSteps = 1000;

} // namespace

// --- CurveWidget --------------------------------------------------------------

CurveWidget::CurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setToolTip(tr("Click the line to add a point, drag a point to bend the curve. "
                  "Drag a point off the square, or press Delete, to remove it."));
}

void CurveWidget::setPoints(const QList<QPointF> &points)
{
    Adjustment a;
    a.curve = points;
    const QList<QPointF> clean = a.normalized().curve;
    if (clean == m_points)
        return;
    m_points = clean;
    if (m_selected >= m_points.size())
        m_selected = -1;
    update();
}

QRectF CurveWidget::plotRect() const
{
    const double side = std::min(width(), height()) - 12.0;
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QPointF CurveWidget::toWidget(const QPointF &p) const
{
    const QRectF r = plotRect();
    return QPointF(r.left() + p.x() * r.width(), r.bottom() - p.y() * r.height());
}

QPointF CurveWidget::fromWidget(const QPointF &pos) const
{
    const QRectF r = plotRect();
    return QPointF((pos.x() - r.left()) / r.width(), (r.bottom() - pos.y()) / r.height());
}

int CurveWidget::pointAt(const QPointF &pos) const
{
    int best = -1;
    double bestDistance = kGrab;
    for (int i = 0; i < m_points.size(); ++i) {
        const double d = QLineF(toWidget(m_points.at(i)), pos).length();
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void CurveWidget::removeSelected()
{
    if (m_selected < 0 || m_points.size() <= 2)
        return;
    m_points.removeAt(m_selected);
    m_selected = -1;
    m_dragging = false;
    update();
    emit pointsChanged(m_points);
}

void CurveWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    p.fillRect(r, palette().color(QPalette::Base));
    QColor grid = palette().color(QPalette::Text);
    grid.setAlpha(40);
    p.setPen(QPen(grid, 1.0));
    for (int i = 1; i < 4; ++i) {
        const double x = r.left() + r.width() * i / 4.0, y = r.top() + r.height() * i / 4.0;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.drawLine(r.bottomLeft(), r.topRight()); // no change
    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);

    QPainterPath path;
    const int steps = std::max(int(r.width()), 2);
    for (int i = 0; i <= steps; ++i) {
        const double x = double(i) / steps;
        const QPointF w = toWidget(QPointF(x, std::clamp(easeletch::curveValue(m_points, x), 0.0, 1.0)));
        i == 0 ? path.moveTo(w) : path.lineTo(w);
    }
    p.setPen(QPen(palette().color(QPalette::Text), 1.6));
    p.drawPath(path);

    for (int i = 0; i < m_points.size(); ++i) {
        const bool sel = i == m_selected;
        p.setPen(QPen(palette().color(sel ? QPalette::Highlight : QPalette::Text), sel ? 2.0 : 1.2));
        p.setBrush(sel ? palette().color(QPalette::Highlight) : palette().color(QPalette::Base));
        p.drawEllipse(toWidget(m_points.at(i)), 4.5, 4.5);
    }
}

void CurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    const QPointF pos = event->position();
    int hit = pointAt(pos);
    if (hit < 0) {
        // A new point, where the press is: on the line or off it.
        const QPointF v = fromWidget(pos);
        if (v.x() < -0.05 || v.x() > 1.05 || v.y() < -0.05 || v.y() > 1.05)
            return;
        const QPointF added(std::clamp(v.x(), 0.0, 1.0), std::clamp(v.y(), 0.0, 1.0));
        int at = 0;
        while (at < m_points.size() && m_points.at(at).x() < added.x())
            ++at;
        // Never two points on one input.
        if ((at < m_points.size() && m_points.at(at).x() - added.x() < kMinGap)
            || (at > 0 && added.x() - m_points.at(at - 1).x() < kMinGap))
            return;
        m_points.insert(at, added);
        hit = at;
        emit pointsChanged(m_points);
    }
    m_selected = hit;
    m_dragging = true;
    update();
}

void CurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_selected < 0)
        return;
    const QPointF pos = event->position();
    // Dragged well off the square: the point goes (two always stay).
    if (m_points.size() > 2 && !plotRect().adjusted(-30, -30, 30, 30).contains(pos)) {
        removeSelected();
        return;
    }
    const QPointF v = fromWidget(pos);
    // Between its neighbours: points keep their order.
    const double lo = m_selected > 0 ? m_points.at(m_selected - 1).x() + kMinGap : 0.0;
    const double hi = m_selected < m_points.size() - 1 ? m_points.at(m_selected + 1).x() - kMinGap : 1.0;
    const QPointF moved(std::clamp(v.x(), lo, hi), std::clamp(v.y(), 0.0, 1.0));
    if (moved == m_points.at(m_selected))
        return;
    m_points[m_selected] = moved;
    update();
    emit pointsChanged(m_points);
}

void CurveWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void CurveWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        removeSelected();
    else
        QWidget::keyPressEvent(event);
}

// --- AdjustPanel --------------------------------------------------------------

QString AdjustPanel::typeName(AdjustmentType type)
{
    switch (type) {
    case AdjustmentType::BrightnessContrast: return tr("Brightness / Contrast");
    case AdjustmentType::Levels: return tr("Levels");
    case AdjustmentType::Curves: return tr("Curves");
    case AdjustmentType::HueSaturation: return tr("Hue / Saturation");
    case AdjustmentType::Exposure: return tr("Exposure");
    case AdjustmentType::BlackWhite: return tr("Black & White");
    default: return QString();
    }
}

AdjustPanel::AdjustPanel(QWidget *parent)
    : QWidget(parent)
{
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_reset = new QToolButton(this);
    m_reset->setText(tr("Reset"));
    m_reset->setToolTip(tr("Put this adjustment's settings back as they started"));
    auto *header = new QHBoxLayout;
    header->addWidget(m_title, 1);
    header->addWidget(m_reset);

    m_pages = new QStackedWidget(this);
    // Page 0: nothing to adjust yet. One button per kind.
    auto *none = new QWidget(m_pages);
    auto *noneLayout = new QVBoxLayout(none);
    noneLayout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(tr("Add an adjustment layer. It changes everything below it, and can be "
                               "re-edited, masked, faded or deleted at any time."), none);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    noneLayout->addWidget(hint);
    auto *grid = new QGridLayout;
    for (int t = 1; t < easeletch::AdjustmentTypeCount; ++t) {
        const auto type = AdjustmentType(t);
        auto *b = new QToolButton(none);
        b->setText(typeName(type));
        b->setObjectName(QStringLiteral("add-") + easeletch::adjustmentKey(type));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [this, type] { emit addRequested(type); });
        grid->addWidget(b, (t - 1) / 2, (t - 1) % 2);
    }
    noneLayout->addLayout(grid);
    noneLayout->addStretch(1);
    m_pages->addWidget(none);
    for (int t = 1; t < easeletch::AdjustmentTypeCount; ++t)
        m_pages->addWidget(makePage(AdjustmentType(t)));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(header);
    layout->addWidget(m_pages, 1);

    connect(m_reset, &QToolButton::clicked, this, [this] {
        m_adjust = Adjustment::make(m_adjust.type);
        setAdjustment(m_adjust);
        emit adjustmentChanged(m_adjust);
    });
    setAdjustment(Adjustment());
}

void AdjustPanel::addRow(QGridLayout *grid, const QString &label, double Adjustment::*field, double min, double max,
                         double scale, int decimals, const QString &suffix, const QString &tip)
{
    QWidget *host = grid->parentWidget();
    const int row = grid->rowCount();
    auto *name = new QLabel(label, host);
    auto *slider = new QSlider(Qt::Horizontal, host);
    slider->setRange(0, kSliderSteps);
    auto *spin = new QDoubleSpinBox(host);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals > 0 ? std::pow(10.0, -decimals) * 5.0 : 1.0);
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    if (!tip.isEmpty()) {
        name->setToolTip(tip);
        slider->setToolTip(tip);
        spin->setToolTip(tip);
    }
    grid->addWidget(name, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    m_rows.append({slider, spin, field, scale});

    connect(slider, &QSlider::valueChanged, this, [this, spin, min, max](int v) {
        if (!m_syncing)
            spin->setValue(min + (max - min) * v / double(kSliderSteps));
    });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, field, scale, min, max](double v) {
        {
            const QSignalBlocker block(slider);
            slider->setValue(int(std::lround((v - min) / (max - min) * kSliderSteps)));
        }
        if (m_syncing)
            return;
        m_adjust.*field = v / scale;
        changed();
    });
}

QWidget *AdjustPanel::makePage(AdjustmentType type)
{
    auto *page = new QWidget(m_pages);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    // QGridLayout needs its parent set before rows are added through it.
    auto *gridHost = new QWidget(page);
    gridHost->setLayout(grid);
    grid->setContentsMargins(0, 0, 0, 0);

    const QString pct = tr("%");
    switch (type) {
    case AdjustmentType::BrightnessContrast:
        addRow(grid, tr("Brightness"), &Adjustment::brightness, -100, 100, 100, 0, pct);
        addRow(grid, tr("Contrast"), &Adjustment::contrast, -100, 100, 100, 0, pct);
        break;
    case AdjustmentType::Levels:
        addRow(grid, tr("Black point"), &Adjustment::inBlack, 0, 255, 255, 0, QString(),
               tr("Everything this dark or darker becomes black"));
        addRow(grid, tr("Midtones"), &Adjustment::gamma, 0.10, 5.00, 1, 2, QString(),
               tr("Above 1 lightens the midtones, below 1 darkens them"));
        addRow(grid, tr("White point"), &Adjustment::inWhite, 0, 255, 255, 0, QString(),
               tr("Everything this light or lighter becomes white"));
        addRow(grid, tr("Output black"), &Adjustment::outBlack, 0, 255, 255, 0, QString(),
               tr("The darkest the result gets"));
        addRow(grid, tr("Output white"), &Adjustment::outWhite, 0, 255, 255, 0, QString(),
               tr("The lightest the result gets"));
        break;
    case AdjustmentType::Curves:
        m_curve = new CurveWidget(page);
        outer->addWidget(m_curve);
        connect(m_curve, &CurveWidget::pointsChanged, this, [this](const QList<QPointF> &points) {
            if (m_syncing)
                return;
            m_adjust.curve = points;
            changed();
        });
        break;
    case AdjustmentType::HueSaturation:
        addRow(grid, tr("Hue"), &Adjustment::hue, -180, 180, 1, 0, tr("°"));
        addRow(grid, tr("Saturation"), &Adjustment::saturation, -100, 100, 100, 0, pct);
        addRow(grid, tr("Lightness"), &Adjustment::lightness, -100, 100, 100, 0, pct);
        break;
    case AdjustmentType::Exposure:
        addRow(grid, tr("Exposure"), &Adjustment::exposure, -5.00, 5.00, 1, 2, tr(" stops"),
               tr("One stop is twice, or half, the light"));
        break;
    case AdjustmentType::BlackWhite:
        addRow(grid, tr("Reds"), &Adjustment::red, -200, 300, 100, 0, pct,
               tr("How light red things come out in the grey"));
        addRow(grid, tr("Greens"), &Adjustment::green, -200, 300, 100, 0, pct);
        addRow(grid, tr("Blues"), &Adjustment::blue, -200, 300, 100, 0, pct);
        break;
    case AdjustmentType::None:
        break;
    }
    outer->addWidget(gridHost);
    outer->addStretch(1);
    return page;
}

void AdjustPanel::syncRows()
{
    for (const Row &r : std::as_const(m_rows))
        r.spin->setValue(m_adjust.*(r.field) * r.scale);
}

void AdjustPanel::setAdjustment(const Adjustment &adjustment)
{
    m_adjust = adjustment.type == AdjustmentType::None ? Adjustment() : adjustment.normalized();
    m_syncing = true;
    m_pages->setCurrentIndex(int(m_adjust.type));
    m_title->setText(m_adjust.type == AdjustmentType::None ? tr("No adjustment layer selected") : typeName(m_adjust.type));
    m_reset->setVisible(m_adjust.type != AdjustmentType::None);
    syncRows();
    if (m_curve)
        m_curve->setPoints(m_adjust.curve);
    m_syncing = false;
}

void AdjustPanel::changed()
{
    const Adjustment clean = m_adjust.normalized();
    if (clean != m_adjust) {
        // Something was out of range (the black point past the white, say):
        // the controls show what was actually set.
        m_adjust = clean;
        m_syncing = true;
        syncRows();
        m_syncing = false;
    }
    emit adjustmentChanged(m_adjust);
}
