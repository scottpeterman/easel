#pragma once

#include "layerstack.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QTreeWidget>
#include <QWidget>

class QComboBox;
class QSlider;
class QSpinBox;
class QToolButton;

// The tree inside the Layers panel; reports when a drag has rearranged it.
class LayerTree : public QTreeWidget
{
    Q_OBJECT

public:
    using QTreeWidget::QTreeWidget;

signals:
    // Around a drop: the view is about to move its items / has moved them.
    void dropStarted();
    void rearranged();

protected:
    void dropEvent(QDropEvent *event) override;
};

// The Layers panel: the stack as a tree, top layer first, with the active
// layer's blend mode and opacity above it and the layer commands below.
// Tick the box to show or hide a layer, tick the Lock column to protect it,
// double-click to rename, drag to reorder or to move into a group.
//
// The panel only reports what the user asked for; the window changes the
// document and calls setStack() again.
class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(QWidget *parent = nullptr);

    // Rebuilds the tree from the stack.
    void setStack(const easeletch::LayerStack *stack);
    // Selects the stack's active layer, without rebuilding the tree.
    void syncActive();
    LayerTree *tree() const { return m_tree; }
    static QString blendModeName(easeletch::BlendMode mode);

signals:
    void activated(int id);
    void visibilityChanged(int id, bool visible);
    void lockChanged(int id, bool locked);
    void renamed(int id, const QString &name);
    void opacityChanged(int id, double opacity);
    void blendChanged(int id, easeletch::BlendMode mode);
    // The whole stack after a drag, bottom to top: (layer id, parent id).
    void rearranged(const QList<QPair<int, int>> &order);
    void addLayerRequested();
    void addGroupRequested();
    void duplicateRequested();
    void raiseRequested();
    void lowerRequested();
    void mergeRequested();
    void deleteRequested();

private:
    void addItems(QTreeWidgetItem *parentItem, int parentId);
    void refreshControls();
    QTreeWidgetItem *itemFor(int id) const;
    void itemChanged(QTreeWidgetItem *item, int column);
    void readTree();
    void collect(QTreeWidgetItem *parentItem, int parentId, QList<QPair<int, int>> &order) const;
    int idOf(const QTreeWidgetItem *item) const;

    const easeletch::LayerStack *m_stack = nullptr;
    LayerTree *m_tree = nullptr;
    QComboBox *m_blend = nullptr;
    QSlider *m_opacity = nullptr;
    QSpinBox *m_opacitySpin = nullptr;
    QToolButton *m_merge = nullptr;
    QToolButton *m_raise = nullptr;
    QToolButton *m_lower = nullptr;
    QToolButton *m_delete = nullptr;
    QSet<int> m_collapsed;
    bool m_updating = false;
};
