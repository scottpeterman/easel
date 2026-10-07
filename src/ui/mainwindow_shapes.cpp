// MainWindow: shapes drawn point by point, and changing them afterwards.

#include "mainwindow.h"

#include "canvasview.h"
#include "colorpanel.h"
#include "selecttools.h"
#include "toolbarrow.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QGuiApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <climits>
#include <cmath>

using easeletch::ShapeNode;
using easeletch::ShapeSettings;

namespace {

// Points sit on whole pixels: lines between them come out crisp.
QPointF onPixel(const QPointF &p)
{
    return QPointF(std::round(p.x()), std::round(p.y()));
}

} // namespace

void MainWindow::createShapeOptions()
{
    m_shapeOptions = new QToolBar(tr("Shape Options"), this);
    m_shapeOptions->setObjectName(QStringLiteral("ShapeOptionsBar"));
    m_shapeOptions->setMovable(false);

    auto *host = new QWidget(m_shapeOptions);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto *title = new QLabel(tr("Shape"), host);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(title);
    row->addSpacing(8);
    row->addWidget(new QLabel(tr("Line"), host));
    m_shapeLine = new QSpinBox(host);
    m_shapeLine->setObjectName(QStringLiteral("shapeLine"));
    m_shapeLine->setRange(0, ShapeSettings::MaxLine);
    m_shapeLine->setSpecialValueText(tr("None"));
    m_shapeLine->setSuffix(tr(" px"));
    m_shapeLine->setToolTip(tr("How heavy the outline is. None: just the fill."));
    m_shapeLineColor = new QToolButton(host);
    m_shapeLineColor->setFixedWidth(36);
    m_shapeLineColor->setToolTip(tr("The outline's colour"));
    m_shapeFilled = new QCheckBox(tr("Fill"), host);
    m_shapeFilled->setToolTip(tr("Fill the shape with the painting colour (the Color panel's)"));
    m_shapeGradient = new QCheckBox(tr("Gradient"), host);
    m_shapeGradient->setObjectName(QStringLiteral("shapeGradient"));
    m_shapeGradient->setToolTip(tr("Fill with a gradient, not the one colour. Click the shape to open it, then drag "
                                   "the two diamonds to aim the gradient."));
    m_shapeGradientPreset = new QComboBox(host);
    m_shapeGradientPreset->setObjectName(QStringLiteral("shapeGradientPreset"));
    m_shapeGradientPreset->setMaxVisibleItems(24);
    m_shapeGradientPreset->setToolTip(tr("The gradient's colours: the same list as the Gradient tool's, where "
                                         "they're edited and saved"));
    m_shapeGradientShape = new QComboBox(host);
    m_shapeGradientShape->setObjectName(QStringLiteral("shapeGradientShape"));
    m_shapeGradientShape->addItems({tr("Linear"), tr("Radial"), tr("Reflected"), tr("Conical")});
    m_shapeGradientShape->setToolTip(tr("How the colours spread"));
    m_shapeCurved = new QCheckBox(tr("Curved"), host);
    m_shapeCurved->setToolTip(tr("A smooth curve through the points, not straight sides"));
    m_shapeClosed = new QCheckBox(tr("Closed"), host);
    m_shapeClosed->setToolTip(tr("The last point joins back to the first. Off: an open line through the points."));
    m_shapeSmooth = new QCheckBox(tr("Smooth"), host);
    m_shapeSmooth->setToolTip(tr("Soft edges. Untick for hard pixels (sprites, pixel art)."));
    m_shapeTaperStart = new QSpinBox(host);
    m_shapeTaperStart->setObjectName(QStringLiteral("shapeTaperStart"));
    m_shapeTaperEnd = new QSpinBox(host);
    m_shapeTaperEnd->setObjectName(QStringLiteral("shapeTaperEnd"));
    for (QSpinBox *box : {m_shapeTaperStart, m_shapeTaperEnd}) {
        box->setRange(0, 100);
        box->setSingleStep(5);
        box->setSuffix(tr("%"));
        box->setSpecialValueText(tr("None"));
    }
    m_shapeTaperStart->setToolTip(tr("An open line thins to a point at its start, as a pen stroke does: how much of "
                                     "the line that takes"));
    m_shapeTaperEnd->setToolTip(tr("An open line thins to a point at its end: how much of the line that takes"));
    m_shapeSnap = new QCheckBox(tr("Snap"), host);
    m_shapeSnap->setObjectName(QStringLiteral("shapeSnap"));
    m_shapeSnap->setToolTip(tr("A point put down or moved near a point of another shape lands exactly on it, so "
                               "lines meet with no gap"));
    auto *hint = new QLabel(tr("Click points; click the first, double-click or press Enter to finish. "
                               "Shift: 15° steps. Click a shape to change it; Ctrl+click to start a new one on it."),
                            host);
    hint->setEnabled(false);
    // Cut short before it makes the window any wider.
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(m_shapeLine);
    row->addWidget(m_shapeLineColor);
    row->addWidget(m_shapeFilled);
    row->addWidget(m_shapeGradient);
    row->addWidget(m_shapeGradientPreset);
    row->addWidget(m_shapeGradientShape);
    row->addWidget(m_shapeCurved);
    row->addWidget(m_shapeClosed);
    row->addWidget(m_shapeSmooth);
    row->addWidget(new QLabel(tr("Taper"), host));
    row->addWidget(m_shapeTaperStart);
    row->addWidget(m_shapeTaperEnd);
    row->addWidget(m_shapeSnap);
    row->addSpacing(12);
    row->addWidget(hint, 1);
    spreadAcrossToolBar(m_shapeOptions, host);

    const auto changed = [this] {
        ShapeOptions o = m_shapeOpts;
        o.line = m_shapeLine->value();
        o.filled = m_shapeFilled->isChecked();
        o.gradient = m_shapeGradient->isChecked();
        o.curved = m_shapeCurved->isChecked();
        o.closed = m_shapeClosed->isChecked();
        o.smooth = m_shapeSmooth->isChecked();
        o.taperStart = m_shapeTaperStart->value();
        o.taperEnd = m_shapeTaperEnd->value();
        o.snap = m_shapeSnap->isChecked();
        setShapeOptions(o);
    };
    for (QSpinBox *box : {m_shapeLine, m_shapeTaperStart, m_shapeTaperEnd})
        connect(box, &QSpinBox::valueChanged, this, changed);
    for (QCheckBox *box : {m_shapeFilled, m_shapeGradient, m_shapeCurved, m_shapeClosed, m_shapeSmooth, m_shapeSnap})
        connect(box, &QCheckBox::toggled, this, changed);
    // Picking a gradient here picks it for the Gradient tool too: there's one
    // current gradient. A shape opened again takes it from then on.
    connect(m_shapeGradientPreset, &QComboBox::activated, this, [this](int i) {
        GradientOptions g = m_gradient;
        g.preset = m_shapeGradientPreset->itemData(i).toString();
        m_shape.ownGradient = false;
        setGradientOptions(g);
    });
    connect(m_shapeGradientShape, &QComboBox::activated, this, [this](int i) {
        GradientOptions g = m_gradient;
        g.shape = easeletch::GradientShape(i);
        m_shape.ownGradient = false;
        // Its line was aimed for the other spread: back to one that suits this.
        if (m_shape.active)
            m_shape.shape.gradientPlaced = false;
        setGradientOptions(g);
    });
    connect(m_shapeLineColor, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_shapeOpts.lineColor, this, tr("Outline colour"));
        if (!c.isValid())
            return;
        ShapeOptions o = m_shapeOpts;
        o.lineColor = c;
        setShapeOptions(o);
    });
    addToolBar(Qt::TopToolBarArea, m_shapeOptions);
    mirrorGradientChoices();
    syncShapeOptions();
}

void MainWindow::mirrorGradientChoices()
{
    if (!m_shapeGradientPreset || !m_gradientPreset)
        return;
    const QSignalBlocker a(m_shapeGradientPreset), b(m_shapeGradientShape);
    m_shapeGradientPreset->clear();
    m_shapeGradientPreset->setIconSize(m_gradientPreset->iconSize());
    for (int i = 0; i < m_gradientPreset->count(); ++i) {
        const QVariant id = m_gradientPreset->itemData(i);
        if (!id.isValid())
            m_shapeGradientPreset->insertSeparator(m_shapeGradientPreset->count());
        else
            m_shapeGradientPreset->addItem(m_gradientPreset->itemIcon(i), m_gradientPreset->itemText(i), id);
    }
    m_shapeGradientPreset->setCurrentIndex(m_gradientPreset->currentIndex());
    m_shapeGradientShape->setCurrentIndex(int(m_gradient.shape));
}

void MainWindow::syncShapeOptions()
{
    if (!m_shapeOptions)
        return;
    const QSignalBlocker a(m_shapeLine), b(m_shapeFilled), c(m_shapeCurved), d(m_shapeClosed), e(m_shapeSmooth);
    m_shapeLine->setValue(m_shapeOpts.line);
    m_shapeFilled->setChecked(m_shapeOpts.filled);
    {
        const QSignalBlocker g(m_shapeGradient);
        m_shapeGradient->setChecked(m_shapeOpts.gradient);
    }
    m_shapeGradient->setEnabled(m_shapeOpts.filled);
    m_shapeGradientPreset->setEnabled(m_shapeOpts.filled && m_shapeOpts.gradient);
    m_shapeGradientShape->setEnabled(m_shapeOpts.filled && m_shapeOpts.gradient);
    m_shapeCurved->setChecked(m_shapeOpts.curved);
    m_shapeClosed->setChecked(m_shapeOpts.closed);
    m_shapeSmooth->setChecked(m_shapeOpts.smooth);
    {
        const QSignalBlocker f(m_shapeTaperStart), g(m_shapeTaperEnd), h(m_shapeSnap);
        m_shapeTaperStart->setValue(m_shapeOpts.taperStart);
        m_shapeTaperEnd->setValue(m_shapeOpts.taperEnd);
        m_shapeSnap->setChecked(m_shapeOpts.snap);
    }
    // A line that joins up with itself has no ends to thin.
    m_shapeTaperStart->setEnabled(!m_shapeOpts.closed);
    m_shapeTaperEnd->setEnabled(!m_shapeOpts.closed);
    m_shapeLineColor->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid palette(mid); }")
                                        .arg(m_shapeOpts.lineColor.name()));
}

void MainWindow::setShapeOptions(const ShapeOptions &options)
{
    m_shapeOpts = options;
    m_shapeOpts.line = std::clamp(m_shapeOpts.line, 0, ShapeSettings::MaxLine);
    m_shapeOpts.taperStart = std::clamp(m_shapeOpts.taperStart, 0, 100);
    m_shapeOpts.taperEnd = std::clamp(m_shapeOpts.taperEnd, 0, 100);
    syncShapeOptions();
    updateShape();
}

QList<QPointF> MainWindow::shapePoints() const
{
    return m_shape.active ? m_shape.shape.points : QList<QPointF>();
}

int MainWindow::shapeLayerAt(const QPointF &pos) const
{
    if (!m_stack)
        return 0;
    const double tolerance = 4.0 / qMax(m_view->zoom(), 0.01);
    const QList<easeletch::Layer> &layers = m_stack->layers();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it)
        if (it->hasShape && easeletch::shapeHit(it->shape, pos, tolerance) && m_stack->isShown(it->id)
            && it->isShape())
            return it->id;
    return 0;
}

void MainWindow::beginShape(const QPointF &first)
{
    if (!m_stack || m_view->isStroking())
        return;
    commitFloating(); // places a shape or text already in progress, or anything else floating
    m_shape = ShapeSession();
    m_shape.before = m_stack->snapshot();

    // A shape gets a layer of its own, above the active one.
    easeletch::Layer l;
    l.name = m_stack->uniqueName(tr("Shape"));
    const easeletch::Layer *active = m_stack->active();
    int id = 0;
    if (active && active->group) {
        id = m_stack->insert(std::move(l), active->id, INT_MAX);
    } else {
        const int parent = active ? active->parent : 0;
        const int at = active ? int(m_stack->children(parent).indexOf(active->id)) + 1 : INT_MAX;
        id = m_stack->insert(std::move(l), parent, at);
    }
    m_stack->setActive(id);
    m_editMask = false;
    m_shape.active = true;
    m_shape.adding = true;
    m_shape.layerId = id;
    m_shape.shape.points = {onPixel(first)};
    m_shape.selected = 0;
    layersChanged();
    updateShape();
}

void MainWindow::addShapePoint(const QPointF &pos)
{
    if (!m_shape.active || m_shape.shape.points.size() >= ShapeSettings::MaxPoints)
        return;
    m_shape.shape.insertPoint(int(m_shape.shape.points.size()), onPixel(pos));
    m_shape.selected = int(m_shape.shape.points.size()) - 1;
    updateShape();
}

bool MainWindow::editShape(int layerId)
{
    if (!m_stack || m_view->isStroking())
        return false;
    commitFloating();
    easeletch::Layer *l = m_stack->layer(layerId);
    if (!l || !l->isShape())
        return false;
    if (m_stack->isLocked(layerId)) {
        statusBar()->showMessage(tr("\"%1\" is locked").arg(l->name), 4000);
        return false;
    }
    const ShapeSettings shape = l->shape;
    // The options show the shape as it is, and its fill is the painting colour.
    m_shapeOpts.line = shape.line;
    m_shapeOpts.lineColor = shape.lineColor;
    m_shapeOpts.filled = shape.filled;
    m_shapeOpts.gradient = shape.hasGradient();
    m_shapeOpts.curved = shape.curved;
    m_shapeOpts.closed = shape.closed;
    m_shapeOpts.smooth = shape.smooth;
    m_shapeOpts.taperStart = shape.taperStart;
    m_shapeOpts.taperEnd = shape.taperEnd;
    syncShapeOptions();
    if (shape.filled && !shape.hasGradient())
        m_color->setColor(shape.fill);

    m_shape = ShapeSession();
    m_shape.before = m_stack->snapshot();
    // It floats again, over an empty layer, exactly as while first drawn.
    l->store.clear();
    m_stack->setActive(layerId);
    m_editMask = false;
    m_shape.active = true;
    m_shape.editing = true;
    m_shape.layerId = layerId;
    m_shape.shape = shape;
    m_shape.ownGradient = shape.hasGradient();
    layersChanged();
    updateShape();
    statusBar()->showMessage(shape.curved
                                 ? tr("Drag a point to move it, a side to add a point, the diamonds beside a point to "
                                      "aim the curve. Ctrl+click a point for a sharp corner. Enter applies, Escape "
                                      "cancels.")
                                 : tr("Drag a point to move it, a side to add a point, inside to move the shape. "
                                      "Delete removes the last point touched. Enter applies, Escape cancels."),
                             10000);
    return true;
}

void MainWindow::updateShape()
{
    if (!m_shape.active || !m_stack)
        return;
    easeletch::Layer *l = m_stack->layer(m_shape.layerId);
    if (!l)
        return;
    m_floating.cancel();
    ShapeSettings &s = m_shape.shape;
    s.line = m_shapeOpts.line;
    s.lineColor = m_shapeOpts.lineColor;
    s.filled = m_shapeOpts.filled;
    s.fill = m_color->color();
    s.gradientFill = m_shapeOpts.gradient;
    if (!m_shape.ownGradient) {
        s.gradientStops = currentGradientStops();
        s.gradientShape = m_gradient.shape;
    }
    s.curved = m_shapeOpts.curved;
    s.closed = m_shapeOpts.closed;
    s.smooth = m_shapeOpts.smooth;
    s.taperStart = m_shapeOpts.taperStart;
    s.taperEnd = m_shapeOpts.taperEnd;
    const easeletch::ShapeLayout layout = easeletch::layoutShape(s);
    if (!layout.image.isNull()) {
        m_floating.paste(&l->store, layout.image, layout.origin, {}, canvasRect());
        m_floatLayer = l->id;
        m_floatMask = false;
    }
    // The points are what shows it's being worked on: no marching ants round it.
    m_view->setSelectionOutline(m_selection.outlines());
    showShapeHandles();
    m_view->refresh();
}

void MainWindow::showShapeHandles()
{
    m_shapeTool->setWorking(m_shape.active);
    if (!m_shape.active) {
        if (!m_xf.active)
            m_view->setHandles({});
        return;
    }
    // A small square on every point: eight screen pixels across whatever the
    // zoom, twelve for the one last touched.
    QList<QPolygonF> handles;
    const QList<QPointF> &points = m_shape.shape.points;
    for (int i = 0; i < points.size(); ++i) {
        const double half = (i == m_shape.selected ? 6.0 : 4.0) / qMax(m_view->zoom(), 0.01);
        const QPointF c = points.at(i);
        QPolygonF square;
        square << c + QPointF(-half, -half) << c + QPointF(half, -half) << c + QPointF(half, half)
               << c + QPointF(-half, half);
        handles << square;
    }
    // A small diamond either side of the point last touched, on a curve:
    // where the line heads as it leaves the point, and where it comes in from.
    for (const auto &[c, side] : shapeCurveHandles()) {
        Q_UNUSED(side);
        const double half = 5.0 / qMax(m_view->zoom(), 0.01);
        QPolygonF diamond;
        diamond << c + QPointF(0, -half) << c + QPointF(half, 0) << c + QPointF(0, half) << c + QPointF(-half, 0);
        handles << diamond;
    }
    // A diamond on each end of the gradient's line, once the shape is being
    // changed (while it's still being drawn, clicks are for its points).
    if (!m_shape.adding && m_shape.shape.hasGradient() && m_shape.shape.points.size() >= 3) {
        const easeletch::Gradient g = m_shape.shape.gradient();
        const double half = 7.0 / qMax(m_view->zoom(), 0.01);
        for (const QPointF &c : {g.from, g.to}) {
            QPolygonF diamond;
            diamond << c + QPointF(0, -half) << c + QPointF(half, 0) << c + QPointF(0, half) << c + QPointF(-half, 0);
            handles << diamond;
        }
    }
    m_view->setHandles(handles);
}

QList<std::pair<QPointF, int>> MainWindow::shapeCurveHandles() const
{
    QList<std::pair<QPointF, int>> out;
    const ShapeSettings &s = m_shape.shape;
    const int i = m_shape.selected;
    if (!m_shape.active || m_shape.adding || !s.curved || i < 0 || i >= s.points.size()
        || s.node(i).kind == ShapeNode::Corner)
        return out;
    const QPointF h = s.handleOut(i);
    if (std::hypot(h.x(), h.y()) < 1e-6)
        return out;
    // The ends of an open line have only the one side.
    const bool loop = s.isLoop();
    if (loop || i + 1 < s.points.size())
        out.append({s.points.at(i) + h, 1});
    if (loop || i > 0)
        out.append({s.points.at(i) - h, 2});
    return out;
}

QPointF MainWindow::shapePointFor(const QPointF &pos, int index) const
{
    const QList<QPointF> &points = m_shape.shape.points;
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && m_shape.active) {
        // In line with the point before it (the one after, for the first).
        const int from = index > 0 ? index - 1 : points.size() > 1 ? 1 : -1;
        if (from >= 0 && from < points.size()) {
            const QPointF anchor = points.at(from);
            const QPointF d = pos - anchor;
            const double step = 15.0 * 3.14159265358979323846 / 180.0;
            const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
            const QPointF along(std::cos(angle), std::sin(angle));
            // As far along that line as the pointer has gone.
            const double length = std::max(0.0, d.x() * along.x() + d.y() * along.y());
            return onPixel(anchor + along * length);
        }
    }
    if (m_shapeOpts.snap && m_stack) {
        const double reach = 8.0 / qMax(m_view->zoom(), 0.01);
        double best = reach;
        QPointF found;
        bool any = false;
        for (const easeletch::Layer &l : m_stack->layers()) {
            if (l.id == m_shape.layerId || !l.hasShape || !l.isShape() || !m_stack->isShown(l.id))
                continue;
            for (const QPointF &p : l.shape.points) {
                const double d = QLineF(pos, p).length();
                if (d <= best) {
                    best = d;
                    found = p;
                    any = true;
                }
            }
        }
        if (any)
            return found;
    }
    return onPixel(pos);
}

void MainWindow::shapePressed(const QPointF &pos)
{
    m_shape.pressAt = pos;
    m_shape.dragHandle = 0;
    m_shape.dragGradient = 0;
    m_shape.dragPoint = -1;
    m_shape.dragWhole = false;
    m_shape.afterRelease = ShapeSession::Nothing;
    const double grab = 8.0 / qMax(m_view->zoom(), 0.01);
    if (!m_shape.active) {
        // Starting (or opening) one changes the layers, which has to wait
        // until the click is over: the canvas is in the middle of it here.
        m_shape.afterRelease = ShapeSession::Start;
        m_shape.pressNew = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
        return;
    }
    ShapeSettings &s = m_shape.shape;
    if (m_shape.adding) {
        // A second click in the same place, or one on the first point, finishes it.
        const bool again = m_shape.lastClick.isValid() && m_shape.lastClick.elapsed() < QApplication::doubleClickInterval()
                           && QLineF(pos, m_shape.lastClickAt).length() * m_view->zoom() < 6.0;
        m_shape.lastClick.start();
        m_shape.lastClickAt = pos;
        if (again) {
            m_shape.afterRelease = ShapeSession::Finish;
            return;
        }
        if (s.points.size() >= 3 && QLineF(pos, s.points.first()).length() <= grab) {
            ShapeOptions o = m_shapeOpts;
            o.closed = true;
            setShapeOptions(o);
            m_shape.afterRelease = ShapeSession::Finish;
            return;
        }
        addShapePoint(shapePointFor(pos, int(s.points.size())));
        m_shape.dragPoint = m_shape.selected; // holding on, it can still be put right
        return;
    }
    // Changing one already placed. The gradient's ends come first: they can
    // sit right on a point.
    if (s.hasGradient() && s.points.size() >= 3) {
        const easeletch::Gradient g = s.gradient();
        const double toFrom = QLineF(pos, g.from).length(), toTo = QLineF(pos, g.to).length();
        if (std::min(toFrom, toTo) <= grab) {
            // From here on the line is where it's put, not worked out from the shape.
            s.gradientFrom = g.from;
            s.gradientTo = g.to;
            s.gradientPlaced = true;
            m_shape.dragGradient = toTo <= toFrom ? 2 : 1;
            return;
        }
    }
    const int point = easeletch::shapePointAt(s, pos, grab);
    // The curve's handles for the point last touched, unless the press is
    // squarely on a point: at a low zoom they can sit right beside one.
    if (point < 0 || QLineF(pos, s.points.at(point)).length() > grab * 0.5) {
        for (const auto &[at, side] : shapeCurveHandles()) {
            if (QLineF(pos, at).length() <= grab) {
                m_shape.dragHandle = side;
                return;
            }
        }
    }
    if (point >= 0 && s.curved && QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        // A sharp corner here, or back to a curve worked out from its neighbours.
        ShapeNode nd = s.node(point);
        nd.kind = nd.kind == ShapeNode::Corner ? ShapeNode::Smooth : ShapeNode::Corner;
        nd.out = {};
        s.setNode(point, nd);
        m_shape.selected = point;
        updateShape();
        return;
    }
    if (point >= 0) {
        m_shape.selected = m_shape.dragPoint = point;
        showShapeHandles();
        m_view->refresh();
        return;
    }
    const int side = easeletch::shapeSideAt(s, pos, grab);
    if (side >= 0 && s.points.size() < ShapeSettings::MaxPoints) {
        s.insertPoint(side + 1, onPixel(pos));
        m_shape.selected = m_shape.dragPoint = side + 1;
        updateShape();
        return;
    }
    if (easeletch::shapeHit(s, pos, grab)) {
        m_shape.dragWhole = true;
        m_shape.dragStart = s.points;
        return;
    }
    // A click away from it puts it down.
    m_shape.afterRelease = ShapeSession::Finish;
}

void MainWindow::shapeDragged(const QPointF &pos)
{
    if (!m_shape.active)
        return;
    ShapeSettings &s = m_shape.shape;
    if (m_shape.dragGradient != 0) {
        (m_shape.dragGradient == 1 ? s.gradientFrom : s.gradientTo) = pos;
        updateShape();
        return;
    }
    if (m_shape.dragHandle != 0 && m_shape.selected >= 0 && m_shape.selected < s.points.size()) {
        // Aimed by hand from here on. The other side stays opposite it.
        const QPointF p = s.points.at(m_shape.selected);
        ShapeNode nd;
        nd.kind = ShapeNode::Handle;
        nd.out = m_shape.dragHandle == 1 ? pos - p : p - pos;
        s.setNode(m_shape.selected, nd);
        updateShape();
        return;
    }
    if (m_shape.dragPoint >= 0 && m_shape.dragPoint < s.points.size()) {
        const QPointF to = shapePointFor(pos, m_shape.dragPoint);
        if (s.points.at(m_shape.dragPoint) == to)
            return;
        s.points[m_shape.dragPoint] = to;
        updateShape();
    } else if (m_shape.dragWhole && m_shape.dragStart.size() == s.points.size()) {
        const QPointF delta = onPixel(pos - m_shape.pressAt);
        const QPointF step = m_shape.dragStart.first() + delta - s.points.first();
        s.translate(step); // the gradient's line goes with it
        updateShape();
    }
}

void MainWindow::shapeReleased(const QPointF &pos)
{
    shapeDragged(pos);
    m_shape.dragHandle = 0;
    m_shape.dragGradient = 0;
    m_shape.dragPoint = -1;
    m_shape.dragWhole = false;
    const ShapeSession::After after = m_shape.afterRelease;
    m_shape.afterRelease = ShapeSession::Nothing;
    if (after == ShapeSession::Nothing)
        return;
    const QPointF at = m_shape.pressAt;
    const bool fresh = m_shape.pressNew;
    m_shape.pressNew = false;
    QTimer::singleShot(0, this, [this, after, at, fresh] {
        if (m_view->tool() != m_shapeTool)
            return;
        if (after == ShapeSession::Finish) {
            commitShape();
        } else if (!m_shape.active) {
            // On a shape already placed, the click opens it; anywhere else
            // (or with Ctrl, to start a line on another's corner) it starts
            // a new one.
            if (const int id = fresh ? 0 : shapeLayerAt(at))
                editShape(id);
            else
                beginShape(shapePointFor(at, 0));
            // A quick second click right here would otherwise count as a double-click.
            m_shape.lastClick.start();
            m_shape.lastClickAt = at;
        }
    });
}

void MainWindow::shapeKey(int key)
{
    if (!m_shape.active || (key != Qt::Key_Backspace && key != Qt::Key_Delete))
        return;
    ShapeSettings &s = m_shape.shape;
    if (m_shape.adding) {
        // Takes back the last point; with none left there's no shape.
        if (s.points.size() <= 1) {
            cancelShape();
            return;
        }
        s.removePoint(int(s.points.size()) - 1);
        m_shape.selected = int(s.points.size()) - 1;
        m_shape.lastClick.invalidate();
        updateShape();
        return;
    }
    if (m_shape.selected < 0 || m_shape.selected >= s.points.size() || s.points.size() <= 2)
        return;
    s.removePoint(m_shape.selected);
    m_shape.selected = std::min(m_shape.selected, int(s.points.size()) - 1);
    updateShape();
}

void MainWindow::commitShape()
{
    if (!m_shape.active)
        return;
    if (!m_floating.isActive() || !m_shape.shape.isDrawable()) {
        cancelShape(); // one point isn't a shape
        return;
    }
    m_floating.commit(); // the pixels stay; the step recorded is the document before
    const bool editing = m_shape.editing;
    if (easeletch::Layer *l = m_stack->layer(m_shape.layerId)) {
        // The layer keeps its points, so they can be moved later.
        l->hasShape = true;
        l->shape = m_shape.shape;
        l->shapePixels = l->store.snapshot();
    }
    m_history.pushState(editing ? tr("Edit Shape") : tr("Shape"), std::move(m_shape.before), *m_stack);
    m_shape = ShapeSession();
    m_moveDragging = false;
    showShapeHandles();
    m_view->setSelectionOutline(m_selection.outlines());
    layersChanged();
}

void MainWindow::cancelShape()
{
    if (!m_shape.active)
        return;
    m_floating.cancel();
    // Back to the document as it was before the shape's layer was made, or
    // before the shape was opened again.
    m_stack->swapState(m_shape.before);
    m_shape = ShapeSession();
    m_moveDragging = false;
    showShapeHandles();
    m_view->setSelectionOutline(m_selection.outlines());
    layersChanged();
}

void MainWindow::loadShapeSettings(QSettings &s)
{
    s.beginGroup(QStringLiteral("shape"));
    m_shapeOpts.line = std::clamp(s.value(QStringLiteral("line"), m_shapeOpts.line).toInt(), 0, ShapeSettings::MaxLine);
    if (const QColor c = QColor::fromString(s.value(QStringLiteral("lineColor")).toString()); c.isValid())
        m_shapeOpts.lineColor = c;
    m_shapeOpts.filled = s.value(QStringLiteral("filled"), m_shapeOpts.filled).toBool();
    m_shapeOpts.gradient = s.value(QStringLiteral("gradient"), m_shapeOpts.gradient).toBool();
    m_shapeOpts.curved = s.value(QStringLiteral("curved"), m_shapeOpts.curved).toBool();
    m_shapeOpts.smooth = s.value(QStringLiteral("smooth"), m_shapeOpts.smooth).toBool();
    m_shapeOpts.taperStart = std::clamp(s.value(QStringLiteral("taperStart"), 0).toInt(), 0, 100);
    m_shapeOpts.taperEnd = std::clamp(s.value(QStringLiteral("taperEnd"), 0).toInt(), 0, 100);
    m_shapeOpts.snap = s.value(QStringLiteral("snap"), m_shapeOpts.snap).toBool();
    s.endGroup();
    syncShapeOptions();
}

void MainWindow::saveShapeSettings(QSettings &s) const
{
    s.beginGroup(QStringLiteral("shape"));
    s.setValue(QStringLiteral("line"), m_shapeOpts.line);
    s.setValue(QStringLiteral("lineColor"), m_shapeOpts.lineColor.name());
    s.setValue(QStringLiteral("filled"), m_shapeOpts.filled);
    s.setValue(QStringLiteral("gradient"), m_shapeOpts.gradient);
    s.setValue(QStringLiteral("curved"), m_shapeOpts.curved);
    s.setValue(QStringLiteral("smooth"), m_shapeOpts.smooth);
    s.setValue(QStringLiteral("taperStart"), m_shapeOpts.taperStart);
    s.setValue(QStringLiteral("taperEnd"), m_shapeOpts.taperEnd);
    s.setValue(QStringLiteral("snap"), m_shapeOpts.snap);
    s.endGroup();
}
