#pragma once

#include "filters.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSlider;
class QTimer;

// A filter's settings. The canvas behind shows the result as they change
// (the window does the filtering: this reports the settings).
class FilterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FilterDialog(const easeletch::Filter &filter, QWidget *parent = nullptr);

    easeletch::Filter filter() const { return m_filter; }
    void setFilter(const easeletch::Filter &filter);
    bool previewEnabled() const;
    static QString typeName(easeletch::FilterType type);

signals:
    // The settings to show on the canvas now. Sent shortly after the last
    // change, so a slider being dragged doesn't queue up a filter per step.
    void previewRequested(const easeletch::Filter &filter);
    // Preview was unticked: the canvas should show the picture as it was.
    void previewCleared();

private:
    struct Row {
        QSlider *slider = nullptr;
        QDoubleSpinBox *spin = nullptr;
    };
    Row addRow(class QGridLayout *grid, const QString &label, double min, double max, int decimals,
               const QString &suffix, const QString &tip);
    void changed();

    easeletch::Filter m_filter;
    Row m_radius, m_amount, m_cell, m_speck, m_tolerance, m_darkness, m_detail, m_ink, m_hardness;
    QCheckBox *m_monochrome = nullptr;
    QCheckBox *m_preview = nullptr;
    QTimer *m_timer = nullptr;
    bool m_syncing = false;
};
