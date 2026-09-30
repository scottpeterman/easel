#pragma once

#include <QColor>
#include <QDialog>
#include <QSize>

class QComboBox;
class QSpinBox;

class NewDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int MaxDimension = 30000;

    explicit NewDocumentDialog(const QSize &initial, QWidget *parent = nullptr);

    QSize canvasSize() const;
    QColor background() const;

private:
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_background = nullptr;
};
