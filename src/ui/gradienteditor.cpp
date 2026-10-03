#include "gradienteditor.h"

#include "color.h"
#include "colorwheel.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using easeletch::GradientStop;
using easeletch::GradientStops;

namespace {

constexpr int kMargin = 9;      // room for half a marker at each end
constexpr int kMarkerHeight = 16;
constexpr int kGrab = 7;        // how near a marker a press takes hold of it

// The gradient over a checkerboard, one colour per column.
void paintGradient(QPainter &p, const QRect &rect, const GradientStops &stops)
{
    if (rect.isEmpty())
        return;
    const int cell = 6;
    for (int y = rect.top(); y <= rect.bottom(); y += cell)
        for (int x = rect.left(); x <= rect.right(); x += cell)
            p.fillRect(QRect(x, y, cell, cell) & rect,
                       (((x - rect.left()) / cell + (y - rect.top()) / cell) & 1) ? QColor(204, 204, 204)
                                                                                 : QColor(255, 255, 255));
    QImage ramp(rect.width(), 1, QImage::Format_ARGB32);
    auto *line = reinterpret_cast<QRgb *>(ramp.scanLine(0));
    const int last = std::max(rect.width() - 1, 1);
    for (int x = 0; x < rect.width(); ++x)
        line[x] = easeletch::pixelToColor(easeletch::gradientPixel(stops, double(x) / last)).rgba();
    p.drawImage(rect, ramp);
}

QString hexOf(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}

} // namespace

GradientBar::GradientBar(QWidget *parent)
    : QWidget(parent)
    , m_stops(easeletch::twoStops(Qt::black, Qt::white))
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GradientBar::setStops(const GradientStops &stops)
{
    m_stops = easeletch::normalizedStops(stops);
    m_selected = std::clamp(m_selected, 0, int(m_stops.size()) - 1);
    update();
    emit stopsChanged();
    emit selectionChanged(m_selected);
}

void GradientBar::setSelected(int index)
{
    index = std::clamp(index, 0, int(m_stops.size()) - 1);
    if (index == m_selected)
        return;
    m_selected = index;
    update();
    emit selectionChanged(m_selected);
}

int GradientBar::addStop(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    const QColor color = easeletch::pixelToColor(easeletch::gradientPixel(m_stops, position));
    int at = 0;
    while (at < m_stops.size() && m_stops.at(at).position <= position)
        ++at;
    m_stops.insert(at, GradientStop{position, color});
    m_selected = at;
    update();
    emit stopsChanged();
    emit selectionChanged(m_selected);
    return at;
}

bool GradientBar::removeStop(int index)
{
    if (m_stops.size() <= 2 || index < 0 || index >= m_stops.size())
        return false;
    m_stops.removeAt(index);
    m_selected = std::clamp(m_selected > index ? m_selected - 1 : m_selected, 0, int(m_stops.size()) - 1);
    update();
    emit stopsChanged();
    emit selectionChanged(m_selected);
    return true;
}

void GradientBar::setStopColor(int index, const QColor &color)
{
    if (index < 0 || index >= m_stops.size() || !color.isValid() || m_stops.at(index).color == color)
        return;
    m_stops[index].color = color;
    update();
    emit stopsChanged();
}

int GradientBar::setStopPosition(int index, double position)
{
    if (index < 0 || index >= m_stops.size())
        return index;
    position = std::clamp(position, 0.0, 1.0);
    if (m_stops.at(index).position == position)
        return index;
    GradientStop moved = m_stops.takeAt(index);
    moved.position = position;
    // Back in among the others, on the side it came from when positions tie.
    int at = 0;
    while (at < m_stops.size()
           && (m_stops.at(at).position < position || (m_stops.at(at).position == position && at < index)))
        ++at;
    m_stops.insert(at, moved);
    const bool reselect = m_selected == index && at != index;
    if (m_selected == index)
        m_selected = at;
    update();
    emit stopsChanged();
    if (reselect)
        emit selectionChanged(m_selected);
    return at;
}

QRect GradientBar::barRect() const
{
    return QRect(kMargin, 4, width() - 2 * kMargin, height() - 8 - kMarkerHeight);
}

double GradientBar::positionAt(int x) const
{
    const QRect r = barRect();
    return r.width() <= 1 ? 0.0 : std::clamp(double(x - r.left()) / (r.width() - 1), 0.0, 1.0);
}

int GradientBar::xOf(double position) const
{
    const QRect r = barRect();
    return r.left() + int(std::lround(position * (r.width() - 1)));
}

int GradientBar::stopAt(const QPoint &pos) const
{
    // The nearest marker within reach; the selected one wins a tie, so a stop
    // sitting on another can still be dragged off it.
    int best = -1, bestDistance = kGrab + 1;
    for (int i = 0; i < m_stops.size(); ++i) {
        const int d = std::abs(xOf(m_stops.at(i).position) - pos.x());
        if (d < bestDistance || (d == bestDistance && i == m_selected)) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

QPixmap GradientBar::swatch(const GradientStops &stops, const QSize &size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintGradient(p, QRect(QPoint(0, 0), size), stops);
    p.setPen(QColor(0, 0, 0, 90));
    p.drawRect(QRect(QPoint(0, 0), size).adjusted(0, 0, -1, -1));
    return pm;
}

void GradientBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = barRect();
    paintGradient(p, r, m_stops);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(r.adjusted(-1, -1, 0, 0));

    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_stops.size(); ++i) {
        // A pointer under the bar, filled with the stop's colour.
        const double x = xOf(m_stops.at(i).position) + 0.5;
        const double top = r.bottom() + 2.5;
        QPolygonF marker;
        marker << QPointF(x, top) << QPointF(x + 6, top + 6) << QPointF(x + 6, top + kMarkerHeight - 3)
               << QPointF(x - 6, top + kMarkerHeight - 3) << QPointF(x - 6, top + 6);
        const bool sel = i == m_selected;
        p.setPen(QPen(sel ? palette().color(QPalette::Highlight) : palette().color(QPalette::WindowText), sel ? 2.0 : 1.0));
        QColor fill = m_stops.at(i).color;
        fill.setAlpha(255);
        p.setBrush(fill);
        p.drawPolygon(marker);
    }
}

void GradientBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    const QPoint pos = event->position().toPoint();
    int hit = stopAt(pos);
    // A press on the bar itself, away from any marker, adds a stop there.
    if (hit < 0 || (pos.y() <= barRect().bottom() && std::abs(xOf(m_stops.at(hit).position) - pos.x()) > 2))
        hit = addStop(positionAt(pos.x()));
    else
        setSelected(hit);
    m_dragging = true;
}

void GradientBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
        setStopPosition(m_selected, positionAt(event->position().toPoint().x()));
}

void GradientBar::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void GradientBar::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        removeStop(m_selected);
        return;
    case Qt::Key_Left:
        setStopPosition(m_selected, m_stops.at(m_selected).position - 0.01);
        return;
    case Qt::Key_Right:
        setStopPosition(m_selected, m_stops.at(m_selected).position + 0.01);
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

GradientDialog::GradientDialog(const GradientStops &stops, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit Gradient"));

    m_bar = new GradientBar(this);
    auto *hint = new QLabel(tr("Click the bar to add a colour. Drag a marker to move it. "
                               "Two markers at the same place make a hard edge."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);

    m_wheel = new ColorWheel(this);
    m_wheel->setFixedSize(200, 200);
    m_hex = new QLineEdit(this);
    m_hex->setMaxLength(7);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    m_opacitySpin = new QSpinBox(this);
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix(tr("%"));
    m_position = new QDoubleSpinBox(this);
    m_position->setRange(0.0, 100.0);
    m_position->setDecimals(1);
    m_position->setSuffix(tr("%"));
    m_position->setKeyboardTracking(false);
    m_delete = new QPushButton(tr("Delete Colour"), this);
    m_delete->setAutoDefault(false);
    auto *reverse = new QPushButton(tr("Reverse"), this);
    reverse->setAutoDefault(false);
    reverse->setToolTip(tr("Turn the gradient back to front"));

    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(m_opacity, 1);
    opacityRow->addWidget(m_opacitySpin);
    auto *form = new QFormLayout;
    form->addRow(tr("Colour"), m_hex);
    form->addRow(tr("Opacity"), opacityRow);
    form->addRow(tr("Position"), m_position);
    auto *side = new QVBoxLayout;
    side->addLayout(form);
    side->addWidget(m_delete);
    side->addWidget(reverse);
    side->addStretch(1);
    auto *middle = new QHBoxLayout;
    middle->addWidget(m_wheel);
    middle->addLayout(side, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *save = buttons->addButton(tr("Save as Preset…"), QDialogButtonBox::ActionRole);
    save->setAutoDefault(false);
    save->setToolTip(tr("Keep this gradient in the list under a name"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_bar);
    layout->addWidget(hint);
    layout->addLayout(middle);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &GradientDialog::saveAsPreset);
    connect(m_bar, &GradientBar::selectionChanged, this, &GradientDialog::syncFromSelection);
    connect(m_bar, &GradientBar::stopsChanged, this, &GradientDialog::syncFromSelection);
    connect(m_wheel, &ColorWheel::colorChanged, this, &GradientDialog::applyColor);
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QString text = m_hex->text().trimmed();
        if (!text.startsWith(QLatin1Char('#')))
            text.prepend(QLatin1Char('#'));
        const QColor c(text);
        if (c.isValid())
            applyColor(c);
        else
            syncFromSelection();
    });
    connect(m_opacity, &QSlider::valueChanged, m_opacitySpin, &QSpinBox::setValue);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_syncing)
            return;
        QColor c = m_bar->stops().at(m_bar->selected()).color;
        c.setAlphaF(float(v / 100.0));
        m_bar->setStopColor(m_bar->selected(), c);
    });
    connect(m_position, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_syncing)
            m_bar->setStopPosition(m_bar->selected(), v / 100.0);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] { m_bar->removeStop(m_bar->selected()); });
    connect(reverse, &QPushButton::clicked, this, [this] {
        const int selected = int(m_bar->stops().size()) - 1 - m_bar->selected();
        m_bar->setStops(easeletch::reversedStops(m_bar->stops()));
        m_bar->setSelected(selected);
    });

    m_bar->setStops(stops);
    syncFromSelection();
}

GradientStops GradientDialog::stops() const
{
    return m_bar->stops();
}

void GradientDialog::syncFromSelection()
{
    const GradientStops stops = m_bar->stops();
    const int i = m_bar->selected();
    if (i < 0 || i >= stops.size())
        return;
    m_syncing = true;
    const GradientStop &s = stops.at(i);
    QColor opaque = s.color;
    opaque.setAlpha(255);
    if (m_wheel->color() != opaque)
        m_wheel->setColor(opaque);
    if (!m_hex->hasFocus())
        m_hex->setText(hexOf(opaque));
    const int percent = int(std::lround(s.color.alphaF() * 100.0));
    {
        const QSignalBlocker block(m_opacity);
        m_opacity->setValue(percent);
    }
    m_opacitySpin->setValue(percent);
    m_position->setValue(s.position * 100.0);
    m_delete->setEnabled(stops.size() > 2);
    m_syncing = false;
}

void GradientDialog::applyColor(const QColor &opaque)
{
    if (m_syncing)
        return;
    // The colour changes; how opaque the stop is stays.
    QColor c = opaque;
    c.setAlpha(m_bar->stops().at(m_bar->selected()).color.alpha());
    m_bar->setStopColor(m_bar->selected(), c);
}

void GradientDialog::saveAsPreset()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save as Preset"), tr("Name:"), QLineEdit::Normal, QString(), &ok).simplified();
    if (!ok || name.isEmpty())
        return;
    m_presetName = name;
    accept();
}
