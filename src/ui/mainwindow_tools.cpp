// MainWindow: free transform, flips and quarter turns.

#include "mainwindow.h"

#include "brushtool.h"
#include "canvasview.h"
#include "edittools.h"
#include "selecttools.h"

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <climits>
#include <cmath>
#include <functional>

using easeletch::FreeTransform;
using easeletch::TransformHandle;

namespace {

// Previews fall back to hard pixels while dragging once a smooth redraw takes
// longer than this; the full-quality one happens when the drag ends.
constexpr qint64 kSmoothPreviewBudgetMs = 40;

} // namespace

void MainWindow::createTransformOptions()
{
    m_transformOptions = new QToolBar(tr("Transform Options"), this);
    m_transformOptions->setObjectName(QStringLiteral("TransformOptionsBar"));
    m_transformOptions->setMovable(false);

    auto *host = new QWidget(m_transformOptions);
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto *title = new QLabel(tr("Transform"), host);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    row->addWidget(title);
    row->addSpacing(8);

    const auto percentBox = [host] {
        auto *box = new QDoubleSpinBox(host);
        box->setRange(-6400.0, 6400.0);
        box->setDecimals(1);
        box->setSuffix(tr("%"));
        box->setKeyboardTracking(false);
        box->setToolTip(tr("100% is the size it was. A negative size flips it."));
        return box;
    };
    row->addWidget(new QLabel(tr("W"), host));
    m_xfWidth = percentBox();
    row->addWidget(m_xfWidth);
    row->addWidget(new QLabel(tr("H"), host));
    m_xfHeight = percentBox();
    row->addWidget(m_xfHeight);
    auto *link = new QCheckBox(tr("Linked"), host);
    link->setChecked(true);
    link->setToolTip(tr("Changing the width or the height changes the other to match"));
    row->addWidget(link);

    row->addSpacing(8);
    row->addWidget(new QLabel(tr("Angle"), host));
    m_xfAngle = new QDoubleSpinBox(host);
    m_xfAngle->setRange(-360.0, 360.0);
    m_xfAngle->setDecimals(1);
    m_xfAngle->setSuffix(tr("°"));
    m_xfAngle->setKeyboardTracking(false);
    row->addWidget(m_xfAngle);

    m_xfSmooth = new QCheckBox(tr("Smooth"), host);
    m_xfSmooth->setToolTip(tr("Blend pixels for smooth edges. Untick to keep hard pixels (sprites, pixel art)."));
    m_xfSmooth->setChecked(m_transformSmooth);
    row->addSpacing(8);
    row->addWidget(m_xfSmooth);

    const auto button = [this, host, row](const QString &text, const QString &tip, auto slot) {
        auto *b = new QToolButton(host);
        b->setText(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };
    row->addSpacing(8);
    button(tr("Flip H"), tr("Flip left to right"), &MainWindow::flipHorizontal);
    button(tr("Flip V"), tr("Flip top to bottom"), &MainWindow::flipVertical);
    button(tr("Turn 90°"), tr("Rotate a quarter turn clockwise"), [this] { rotateQuarter(1); });
    row->addSpacing(12);
    button(tr("Apply"), tr("Apply the transform (Enter)"), &MainWindow::commitFloating);
    button(tr("Cancel"), tr("Put everything back (Escape)"), &MainWindow::cancelFloating);
    row->addStretch(1);
    m_transformOptions->addWidget(host);

    // A linked edit keeps the shape: the other side changes by the same factor.
    connect(m_xfWidth, &QDoubleSpinBox::valueChanged, this, [this, link](double v) {
        if (!m_xf.active || v == 0.0)
            return;
        const double sx = v / 100.0;
        const double sy = link->isChecked() && m_xf.box.scaleX != 0.0
                              ? m_xf.box.scaleY * std::abs(sx / m_xf.box.scaleX)
                              : m_xf.box.scaleY;
        setTransform(sx, sy, m_xf.box.angle);
    });
    connect(m_xfHeight, &QDoubleSpinBox::valueChanged, this, [this, link](double v) {
        if (!m_xf.active || v == 0.0)
            return;
        const double sy = v / 100.0;
        const double sx = link->isChecked() && m_xf.box.scaleY != 0.0
                              ? m_xf.box.scaleX * std::abs(sy / m_xf.box.scaleY)
                              : m_xf.box.scaleX;
        setTransform(sx, sy, m_xf.box.angle);
    });
    connect(m_xfAngle, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_xf.active)
            setTransform(m_xf.box.scaleX, m_xf.box.scaleY, v);
    });
    connect(m_xfSmooth, &QCheckBox::toggled, this, &MainWindow::setTransformSmooth);

    addToolBar(Qt::TopToolBarArea, m_transformOptions);
}

bool MainWindow::beginTransform()
{
    return startTransform(true);
}

bool MainWindow::startTransform(bool interactive)
{
    if (!m_stack || m_view->isStroking())
        return false;
    if (m_xf.active)
        return true;
    if (m_text.active)
        commitFloating(); // text being typed is placed first, then it's the layer that's transformed
    // Pixels already floating (a paste being placed) are transformed as they are.
    const bool wasFloating = m_floating.isActive();
    const bool hadSelection = !m_selection.isEmpty();
    CanvasTool *previous = m_view->tool();
    if (!liftForMove())
        return false;
    if (!wasFloating)
        m_floatLabel = tr("Transform");

    m_xf = TransformSession();
    m_xf.active = true;
    m_xf.source = m_floating.content();
    m_xf.shape = m_floating.selection().translated(-m_floating.position());
    m_xf.hadSelection = hadSelection || wasFloating;
    m_xf.box.size = m_xf.source.size();
    m_xf.box.center = QPointF(m_floating.position())
                      + QPointF(m_xf.source.width() / 2.0, m_xf.source.height() / 2.0);
    if (!interactive)
        return true;

    m_xf.previousTool = previous == m_transformTool ? m_move : previous;
    activateTool(m_transformTool, false);
    updateTransformOutline();
    syncTransformOptions();
    statusBar()->showMessage(tr("Drag a corner to scale (Shift: any shape), outside the box to rotate "
                                "(Shift: 15° steps), inside it to move. Enter applies, Escape cancels."),
                             10000);
    return true;
}

void MainWindow::endTransform()
{
    if (!m_xf.active)
        return;
    CanvasTool *previous = m_xf.previousTool;
    m_xf = TransformSession();
    m_view->setHandles({});
    syncTransformOptions();
    if (previous && m_view->tool() == m_transformTool)
        activateTool(previous, previous == m_brush);
}

void MainWindow::applyTransform(bool final)
{
    if (!m_xf.active || !m_floating.isActive())
        return;
    if (m_xf.box.isIdentity()) {
        const QPointF corner = m_xf.box.center
                               - QPointF(m_xf.source.width() / 2.0, m_xf.source.height() / 2.0);
        m_floating.replace(m_xf.source, m_xf.shape,
                           QPoint(int(std::lround(corner.x())), int(std::lround(corner.y()))));
    } else {
        const QTransform matrix = m_xf.box.matrix();
        const bool smooth = m_transformSmooth && (final || !m_xf.changed || m_lastSmoothMs <= kSmoothPreviewBudgetMs);
        QElapsedTimer timer;
        timer.start();
        QPoint origin;
        const QImage image = easeletch::transformImage(m_xf.source, matrix, smooth, &origin, canvasRect());
        if (smooth)
            m_lastSmoothMs = timer.elapsed();
        m_floating.replace(image, easeletch::transformShape(m_xf.shape, m_xf.source.size(), matrix, canvasRect()),
                           origin);
    }
    m_xf.changed = true;
    m_floatStart = QPoint(INT_MIN, INT_MIN); // committing records a step even if it ends where it began
    m_selection = m_floating.selection();
    updateSelectionActions();
    if (m_view->tool() == m_transformTool)
        updateTransformOutline();
    syncTransformOptions();
    m_view->refresh();
}

void MainWindow::updateTransformOutline()
{
    if (!m_xf.active)
        return;
    // The box, and a small square on each handle: eight screen pixels wide,
    // whatever the zoom.
    const FreeTransform &box = m_xf.box;
    QPolygonF frame = box.corners();
    frame << frame.first();
    QList<QPolygonF> handles;

    const double half = 4.0 / qMax(m_view->zoom(), 0.01);
    const QTransform turn = QTransform().rotate(box.angle);
    const bool small = std::abs(box.size.width() * box.scaleX) < half * 8.0
                       || std::abs(box.size.height() * box.scaleY) < half * 8.0;
    const auto square = [&](TransformHandle h) {
        const QPointF c = easeletch::handlePosition(box, h);
        QPolygonF s;
        for (const QPointF &d : {QPointF(-half, -half), QPointF(half, -half), QPointF(half, half),
                                 QPointF(-half, half)})
            s << c + turn.map(d);
        handles << s;
    };
    for (const TransformHandle h : {TransformHandle::TopLeft, TransformHandle::TopRight,
                                    TransformHandle::BottomRight, TransformHandle::BottomLeft})
        square(h);
    if (!small) {
        for (const TransformHandle h : {TransformHandle::Top, TransformHandle::Right, TransformHandle::Bottom,
                                        TransformHandle::Left})
            square(h);
    }
    m_view->setSelectionOutline({frame});
    m_view->setHandles(handles);
}

void MainWindow::syncTransformOptions()
{
    if (!m_xfWidth)
        return;
    const QSignalBlocker b1(m_xfWidth), b2(m_xfHeight), b3(m_xfAngle), b4(m_xfSmooth);
    m_xfWidth->setValue(m_xf.active ? m_xf.box.scaleX * 100.0 : 100.0);
    m_xfHeight->setValue(m_xf.active ? m_xf.box.scaleY * 100.0 : 100.0);
    m_xfAngle->setValue(m_xf.active ? m_xf.box.angle : 0.0);
    m_xfSmooth->setChecked(m_transformSmooth);
}

void MainWindow::setTransform(double scaleX, double scaleY, double angle)
{
    if (!m_xf.active || scaleX == 0.0 || scaleY == 0.0)
        return;
    m_xf.box.scaleX = scaleX;
    m_xf.box.scaleY = scaleY;
    m_xf.box.angle = 0.0;
    m_xf.box.rotate(angle);
    applyTransform(true);
}

void MainWindow::setTransformSmooth(bool smooth)
{
    if (m_transformSmooth == smooth)
        return;
    m_transformSmooth = smooth;
    if (m_xf.active && m_xf.changed)
        applyTransform(true);
    else
        syncTransformOptions();
}

void MainWindow::quickTransform(const QString &label, void (FreeTransform::*change)())
{
    if (!m_stack || m_view->isStroking())
        return;
    if (m_xf.active) {
        (m_xf.box.*change)();
        applyTransform(true);
        return;
    }
    const bool wasFloating = m_floating.isActive() && !m_text.active;
    if (!startTransform(false))
        return;
    (m_xf.box.*change)();
    applyTransform(true);
    if (wasFloating) {
        // A paste still being placed stays floating, now flipped or turned.
        m_xf = TransformSession();
        showFloatingOutline();
        m_view->refresh();
    } else {
        m_floatLabel = label;
        commitFloating();
    }
}

void MainWindow::flipHorizontal()
{
    quickTransform(tr("Flip Horizontal"), &FreeTransform::flipHorizontal);
}

void MainWindow::flipVertical()
{
    quickTransform(tr("Flip Vertical"), &FreeTransform::flipVertical);
}

void MainWindow::rotateQuarter(int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 1)
        quickTransform(tr("Rotate 90° Right"), &FreeTransform::quarterRight);
    else if (turns == 3)
        quickTransform(tr("Rotate 90° Left"), &FreeTransform::quarterLeft);
    else if (turns == 2)
        quickTransform(tr("Rotate 180°"), &FreeTransform::halfTurn);
}

void MainWindow::transformPressed(const QPointF &pos)
{
    if (!m_xf.active)
        return;
    m_xf.handle = easeletch::hitTest(m_xf.box, pos, 8.0 / qMax(m_view->zoom(), 0.01));
    m_xf.start = m_xf.box;
    m_xf.dragFrom = pos;
    m_xf.dragging = true;
}

void MainWindow::transformDragged(const QPointF &pos)
{
    if (!m_xf.active || !m_xf.dragging)
        return;
    const bool shift = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    m_xf.box = easeletch::dragHandle(m_xf.start, m_xf.handle, m_xf.dragFrom, pos, shift);
    applyTransform(false);
}

void MainWindow::transformReleased(const QPointF &pos)
{
    if (!m_xf.active || !m_xf.dragging)
        return;
    const bool shift = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    m_xf.box = easeletch::dragHandle(m_xf.start, m_xf.handle, m_xf.dragFrom, pos, shift);
    m_xf.dragging = false;
    // A click that moved nothing isn't a change.
    if (m_xf.changed || !(m_xf.box.isIdentity() && m_xf.box.center == m_xf.start.center))
        applyTransform(true);
}
