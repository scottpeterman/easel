#include "newdocumentdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

NewDocumentDialog::NewDocumentDialog(const QSize &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Image"));

    m_width = new QSpinBox(this);
    m_width->setRange(1, MaxDimension);
    m_width->setSuffix(tr(" px"));
    m_width->setValue(initial.width());

    m_height = new QSpinBox(this);
    m_height->setRange(1, MaxDimension);
    m_height->setSuffix(tr(" px"));
    m_height->setValue(initial.height());

    m_background = new QComboBox(this);
    m_background->addItem(tr("White"), QColor(Qt::white));
    m_background->addItem(tr("Transparent"), QColor(0, 0, 0, 0));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout(this);
    form->addRow(tr("Width"), m_width);
    form->addRow(tr("Height"), m_height);
    form->addRow(tr("Background"), m_background);
    form->addRow(buttons);
}

QSize NewDocumentDialog::canvasSize() const
{
    return QSize(m_width->value(), m_height->value());
}

QColor NewDocumentDialog::background() const
{
    return m_background->currentData().value<QColor>();
}
