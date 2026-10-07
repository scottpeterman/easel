#include "toolbarrow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QToolBar>

void spreadAcrossToolBar(QToolBar *bar, QWidget *row)
{
    QLayout *from = row->layout();
    Q_ASSERT(from);
    const int spacing = qMax(from->spacing(), 0);
    const QMargins margins = from->contentsMargins();

    QWidget *group = nullptr;
    QHBoxLayout *groupRow = nullptr;
    int gap = margins.left(); // space owed before the next group
    const auto finish = [&] {
        if (group)
            bar->addWidget(group);
        group = nullptr;
        groupRow = nullptr;
    };

    while (QLayoutItem *item = from->takeAt(0)) {
        QWidget *widget = item->widget();
        if (!widget) {
            // Spacing between controls; a stretch has no fixed width and is dropped.
            if (item->spacerItem() && !(item->expandingDirections() & Qt::Horizontal))
                gap += item->sizeHint().width();
            delete item;
            finish();
            continue;
        }
        delete item;

        if (!group) {
            group = new QWidget(bar);
            groupRow = new QHBoxLayout(group);
            groupRow->setContentsMargins(gap, margins.top(), spacing, margins.bottom());
            groupRow->setSpacing(spacing);
            gap = 0;
            // Shrinks if it can, never stretches to fill a wide bar...
            group->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        }
        groupRow->addWidget(widget);

        // ... unless it holds something made to be cut short, like a hint.
        const QSizePolicy::Policy policy = widget->sizePolicy().horizontalPolicy();
        if (policy == QSizePolicy::Ignored)
            group->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

        // A label waits for the control it names.
        const bool namesTheNext = qobject_cast<QLabel *>(widget) && policy != QSizePolicy::Ignored
                                  && from->count() > 0 && from->itemAt(0)->widget();
        if (!namesTheNext)
            finish();
    }
    finish();
    delete row;
}
