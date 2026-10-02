#include "layerpanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDropEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using easeletch::BlendMode;
using easeletch::Layer;

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kNameColumn = 0;
constexpr int kLockColumn = 1;
constexpr int kMaskColumn = 2;

} // namespace

void LayerTree::dropEvent(QDropEvent *event)
{
    emit dropStarted();
    QTreeWidget::dropEvent(event);
    // Let the view finish moving its items before anyone rebuilds it.
    QMetaObject::invokeMethod(this, &LayerTree::rearranged, Qt::QueuedConnection);
}

bool LayerTree::event(QEvent *event)
{
    // The window's Delete shortcut clears selected pixels. With the layer
    // list focused, Delete means the layer: claim the key so it arrives here
    // as a key press instead. (Not while a name is being edited: then the
    // editor has the focus, not the list.)
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    return QTreeWidget::event(event);
}

void LayerTree::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
        emit deletePressed();
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

QString LayerPanel::blendModeName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        return tr("Normal");
    case BlendMode::Multiply:
        return tr("Multiply");
    case BlendMode::Screen:
        return tr("Screen");
    case BlendMode::Overlay:
        return tr("Overlay");
    case BlendMode::SoftLight:
        return tr("Soft Light");
    case BlendMode::Darken:
        return tr("Darken");
    case BlendMode::Lighten:
        return tr("Lighten");
    case BlendMode::ColorDodge:
        return tr("Color Dodge");
    case BlendMode::ColorBurn:
        return tr("Color Burn");
    case BlendMode::Difference:
        return tr("Difference");
    case BlendMode::Hue:
        return tr("Hue");
    case BlendMode::Color:
        return tr("Color");
    }
    return {};
}

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(4, 4, 4, 4);
    column->setSpacing(4);

    auto *top = new QHBoxLayout;
    top->setSpacing(4);
    m_blend = new QComboBox(this);
    for (int i = 0; i < easeletch::BlendModeCount; ++i)
        m_blend->addItem(blendModeName(BlendMode(i)));
    m_blend->setToolTip(tr("How this layer mixes with the ones below it"));
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    m_opacity->setToolTip(tr("Layer opacity"));
    m_opacitySpin = new QSpinBox(this);
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix(tr("%"));
    m_opacitySpin->setKeyboardTracking(false);
    top->addWidget(m_blend);
    top->addWidget(m_opacity, 1);
    top->addWidget(m_opacitySpin);
    column->addLayout(top);

    m_tree = new LayerTree(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Layer"), tr("Lock"), tr("Mask")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kLockColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kMaskColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionsMovable(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    column->addWidget(m_tree, 1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    const auto button = [&](const QString &text, const QString &tip, void (LayerPanel::*signal)()) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, signal);
        buttons->addWidget(b);
        return b;
    };
    button(tr("New"), tr("New layer above this one (Ctrl+Shift+N)"), &LayerPanel::addLayerRequested);
    button(tr("Group"), tr("Put this layer in a new group (Ctrl+G)"), &LayerPanel::addGroupRequested);
    button(tr("Copy"), tr("Duplicate this layer (Ctrl+J)"), &LayerPanel::duplicateRequested);
    m_raise = button(tr("▲"), tr("Move up"), &LayerPanel::raiseRequested);
    m_lower = button(tr("▼"), tr("Move down"), &LayerPanel::lowerRequested);
    m_merge = button(tr("Merge"), tr("Merge into the layer below, or merge a group into one layer (Ctrl+E)"),
                     &LayerPanel::mergeRequested);
    buttons->addStretch(1);
    m_delete = button(tr("Delete"), tr("Delete this layer (the Delete key, when nothing is selected on the canvas)"),
                      &LayerPanel::deleteRequested);
    column->addLayout(buttons);

    // Mask commands for the active layer.
    buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    m_addMask = button(tr("Add Mask"),
                       tr("Give this layer a mask. With a selection, the mask shows only what's selected."),
                       &LayerPanel::addMaskRequested);
    m_editMask = new QToolButton(this);
    m_editMask->setText(tr("Paint Mask"));
    m_editMask->setToolTip(tr("Paint on the mask instead of the layer (Ctrl+M): black hides, white shows"));
    m_editMask->setCheckable(true);
    m_editMask->setAutoRaise(true);
    m_editMask->setFocusPolicy(Qt::NoFocus);
    connect(m_editMask, &QToolButton::clicked, this, &LayerPanel::editMaskToggled);
    buttons->addWidget(m_editMask);
    m_applyMask = button(tr("Apply"), tr("Erase what the mask hides and remove the mask"),
                         &LayerPanel::applyMaskRequested);
    buttons->addStretch(1);
    m_deleteMask = button(tr("Remove"), tr("Remove the mask; the layer shows whole again"),
                          &LayerPanel::deleteMaskRequested);
    column->addLayout(buttons);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!m_updating && item)
            emit activated(idOf(item));
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayerPanel::itemChanged);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (!m_updating)
            m_collapsed.insert(idOf(item));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (!m_updating)
            m_collapsed.remove(idOf(item));
    });
    // Moving rows makes the view pick another current row; that isn't the
    // user choosing a layer.
    connect(m_tree, &LayerTree::dropStarted, this, [this] { m_updating = true; });
    connect(m_tree, &LayerTree::rearranged, this, &LayerPanel::readTree);
    connect(m_tree, &LayerTree::deletePressed, this, &LayerPanel::deleteRequested);

    connect(m_opacity, &QSlider::valueChanged, m_opacitySpin, &QSpinBox::setValue);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_opacity);
        m_opacity->setValue(v);
        if (!m_updating && m_stack && m_stack->active())
            emit opacityChanged(m_stack->activeId(), v / 100.0);
    });
    connect(m_blend, &QComboBox::activated, this, [this](int index) {
        if (!m_updating && m_stack && m_stack->active())
            emit blendChanged(m_stack->activeId(), BlendMode(index));
    });
}

int LayerPanel::idOf(const QTreeWidgetItem *item) const
{
    return item ? item->data(kNameColumn, kIdRole).toInt() : 0;
}

void LayerPanel::addItems(QTreeWidgetItem *parentItem, int parentId)
{
    const QList<int> children = m_stack->children(parentId);
    // The list runs bottom to top; the panel shows the top layer first.
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        const Layer *l = m_stack->layer(*it);
        auto *item = new QTreeWidgetItem(parentItem);
        item->setData(kNameColumn, kIdRole, l->id);
        item->setText(kNameColumn, l->name);
        item->setCheckState(kNameColumn, l->visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(kLockColumn, l->locked ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(kNameColumn, tr("Tick to show. Double-click to rename. Drag to reorder."));
        item->setToolTip(kLockColumn, tr("Locked layers can't be painted on or edited"));
        if (l->hasMask) {
            item->setCheckState(kMaskColumn, l->maskEnabled ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(kMaskColumn, tr("This layer has a mask. Untick to see the layer without it."));
        }
        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable
                              | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
        if (l->group) {
            flags |= Qt::ItemIsDropEnabled;
            item->setIcon(kNameColumn, style()->standardIcon(QStyle::SP_DirIcon));
            QFont f = item->font(kNameColumn);
            f.setBold(true);
            item->setFont(kNameColumn, f);
        }
        item->setFlags(flags);
        if (l->group) {
            addItems(item, l->id);
            item->setExpanded(!m_collapsed.contains(l->id));
        }
        if (l->id == m_stack->activeId())
            m_tree->setCurrentItem(item);
    }
}

void LayerPanel::setStack(const easeletch::LayerStack *stack)
{
    m_stack = stack;
    m_updating = true;
    m_tree->clear();
    if (m_stack)
        addItems(m_tree->invisibleRootItem(), 0);
    m_updating = false;
    refreshControls();
}

QTreeWidgetItem *LayerPanel::itemFor(int id) const
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if (idOf(*it) == id)
            return *it;
    return nullptr;
}

void LayerPanel::syncActive()
{
    if (m_stack) {
        QTreeWidgetItem *item = itemFor(m_stack->activeId());
        if (item && item != m_tree->currentItem()) {
            m_updating = true;
            m_tree->setCurrentItem(item);
            m_updating = false;
        }
    }
    refreshControls();
}

void LayerPanel::setEditingMask(bool on)
{
    m_editingMask = on;
    refreshControls();
}

void LayerPanel::refreshControls()
{
    m_updating = true;
    const Layer *active = m_stack ? m_stack->active() : nullptr;
    m_blend->setEnabled(active != nullptr);
    m_opacity->setEnabled(active != nullptr);
    m_opacitySpin->setEnabled(active != nullptr);
    if (active) {
        m_blend->setCurrentIndex(int(active->blend));
        m_opacitySpin->setValue(int(std::lround(active->opacity * 100.0)));
        const QList<int> siblings = m_stack->children(active->parent);
        const int at = int(siblings.indexOf(active->id));
        m_raise->setEnabled(at < siblings.size() - 1 || active->parent != 0);
        m_lower->setEnabled(at > 0 || active->parent != 0);
        const bool below = at > 0 && !m_stack->layer(siblings.at(at - 1))->group;
        m_merge->setEnabled(active->group || below);
        m_delete->setEnabled(m_stack->count() > 1);
    }
    m_addMask->setEnabled(active && !active->hasMask);
    m_editMask->setEnabled(active && active->hasMask);
    m_editMask->setChecked(active && active->hasMask && m_editingMask);
    m_applyMask->setEnabled(active && active->hasMask && !active->group);
    m_deleteMask->setEnabled(active && active->hasMask);
    m_updating = false;
}

void LayerPanel::itemChanged(QTreeWidgetItem *item, int column)
{
    if (m_updating || !m_stack)
        return;
    const Layer *l = m_stack->layer(idOf(item));
    if (!l)
        return;
    if (column == kLockColumn) {
        const bool locked = item->checkState(kLockColumn) == Qt::Checked;
        if (locked != l->locked)
            QMetaObject::invokeMethod(this, [this, id = l->id, locked] { emit lockChanged(id, locked); },
                                      Qt::QueuedConnection);
        return;
    }
    if (column == kMaskColumn) {
        const bool enabled = item->checkState(kMaskColumn) == Qt::Checked;
        if (l->hasMask && enabled != l->maskEnabled)
            QMetaObject::invokeMethod(this, [this, id = l->id, enabled] { emit maskEnabledChanged(id, enabled); },
                                      Qt::QueuedConnection);
        return;
    }
    const bool visible = item->checkState(kNameColumn) == Qt::Checked;
    // Reported once the view has finished with the item: the window answers
    // by rebuilding the tree.
    if (visible != l->visible) {
        QMetaObject::invokeMethod(this, [this, id = l->id, visible] { emit visibilityChanged(id, visible); },
                                  Qt::QueuedConnection);
        return;
    }
    const QString name = item->text(kNameColumn).trimmed();
    if (name.isEmpty()) {
        const QSignalBlocker block(m_tree);
        item->setText(kNameColumn, l->name);
    } else if (name != l->name) {
        QMetaObject::invokeMethod(this, [this, id = l->id, name] { emit renamed(id, name); }, Qt::QueuedConnection);
    }
}

void LayerPanel::collect(QTreeWidgetItem *parentItem, int parentId, QList<QPair<int, int>> &order) const
{
    for (int i = parentItem->childCount() - 1; i >= 0; --i) {
        QTreeWidgetItem *item = parentItem->child(i);
        order.append({idOf(item), parentId});
        collect(item, idOf(item), order);
    }
}

void LayerPanel::readTree()
{
    m_updating = false;
    if (!m_stack)
        return;
    QList<QPair<int, int>> order;
    collect(m_tree->invisibleRootItem(), 0, order);
    emit rearranged(order);
}
