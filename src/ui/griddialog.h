#pragma once

#include <QDialog>
#include <QPoint>
#include <QSize>

class QCheckBox;
class QSettings;
class QSpinBox;

struct GridSettings {
    bool pixel = true;   // pixel grid from 600% zoom
    bool cells = false;  // sprite cell grid
    QSize cell{32, 32};
    QPoint offset;       // where the first cell starts
    bool snap = true;    // selections snap to cells while the cell grid shows

    void load(QSettings &s);
    void save(QSettings &s) const;
};

// Sprite grid settings. Changes apply live (settingsChanged) so the grid can
// be lined up with a sheet; Cancel restores what was there before.
class GridDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GridDialog(const GridSettings &initial, QWidget *parent = nullptr);

    GridSettings settings() const;

signals:
    void settingsChanged(const GridSettings &settings);

private:
    GridSettings m_base;
    QSpinBox *m_cellW = nullptr;
    QSpinBox *m_cellH = nullptr;
    QSpinBox *m_offsetX = nullptr;
    QSpinBox *m_offsetY = nullptr;
    QCheckBox *m_snap = nullptr;
};
