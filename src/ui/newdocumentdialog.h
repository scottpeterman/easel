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
    // The paper chosen (see paper.h), or empty for plain white or transparent.
    QString paper() const;

private:
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_background = nullptr;
};
