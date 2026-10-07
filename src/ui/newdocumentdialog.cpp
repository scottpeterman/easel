#include "newdocumentdialog.h"

#include "paper.h"

#include <QComboBox>
#include <QIcon>
#include <QPixmap>
#include <QSettings>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

namespace {

constexpr int KeyRole = Qt::UserRole + 1;   // what's remembered as the last choice
constexpr int PaperRole = Qt::UserRole + 2; // the paper's id; empty for a plain background

} // namespace

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
    m_background->setObjectName(QStringLiteral("newBackground"));
    m_background->addItem(tr("White"), QColor(Qt::white));
    m_background->setItemData(0, QStringLiteral("white"), KeyRole);
    m_background->addItem(tr("Transparent"), QColor(0, 0, 0, 0));
    m_background->setItemData(1, QStringLiteral("transparent"), KeyRole);
    // Papers: each a sheet with its own colour and surface, shown as a patch of itself.
    m_background->insertSeparator(m_background->count());
    const QSize patch(44, 28);
    m_background->setIconSize(patch);
    for (const easeletch::PaperStyle &p : easeletch::paperStyles()) {
        m_background->addItem(QIcon(QPixmap::fromImage(easeletch::paperPreview(p, patch))),
                              tr("%1 paper").arg(tr(qPrintable(p.name))), p.base);
        m_background->setItemData(m_background->count() - 1, p.id, KeyRole);
        m_background->setItemData(m_background->count() - 1, p.id, PaperRole);
    }
    // The one used last: a sketchbook is mostly one paper.
    const QString last = QSettings().value(QStringLiteral("newDocument/background")).toString();
    const int found = m_background->findData(last, KeyRole);
    if (!last.isEmpty() && found >= 0)
        m_background->setCurrentIndex(found);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings().setValue(QStringLiteral("newDocument/background"), m_background->currentData(KeyRole));
        accept();
    });
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

QString NewDocumentDialog::paper() const
{
    return m_background->currentData(PaperRole).toString();
}
