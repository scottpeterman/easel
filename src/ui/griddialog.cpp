#include "griddialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

void GridSettings::load(QSettings &s)
{
    s.beginGroup(QStringLiteral("grid"));
    pixel = s.value(QStringLiteral("pixel"), pixel).toBool();
    cells = s.value(QStringLiteral("cells"), cells).toBool();
    cell = s.value(QStringLiteral("cell"), cell).toSize();
    offset = s.value(QStringLiteral("offset"), offset).toPoint();
    snap = s.value(QStringLiteral("snap"), snap).toBool();
    if (cell.width() < 1 || cell.height() < 1)
        cell = QSize(32, 32);
    s.endGroup();
}

void GridSettings::save(QSettings &s) const
{
    s.beginGroup(QStringLiteral("grid"));
    s.setValue(QStringLiteral("pixel"), pixel);
    s.setValue(QStringLiteral("cells"), cells);
    s.setValue(QStringLiteral("cell"), cell);
    s.setValue(QStringLiteral("offset"), offset);
    s.setValue(QStringLiteral("snap"), snap);
    s.endGroup();
}

namespace {

QSpinBox *spin(int min, int max, int value, QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setSuffix(QObject::tr(" px"));
    return s;
}

QWidget *pair(QSpinBox *a, QSpinBox *b, const QString &sep, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(a);
    row->addWidget(new QLabel(sep, w));
    row->addWidget(b);
    return w;
}

} // namespace

GridDialog::GridDialog(const GridSettings &initial, QWidget *parent)
    : QDialog(parent)
    , m_base(initial)
{
    setWindowTitle(tr("Sprite Grid"));
    m_cellW = spin(1, 8192, initial.cell.width(), this);
    m_cellH = spin(1, 8192, initial.cell.height(), this);
    m_offsetX = spin(0, 8191, initial.offset.x(), this);
    m_offsetY = spin(0, 8191, initial.offset.y(), this);
    m_snap = new QCheckBox(tr("Snap selections to cells"), this);
    m_snap->setChecked(initial.snap);
    m_snap->setToolTip(tr("Selections cover whole cells; a click selects one cell"));

    auto *form = new QFormLayout;
    form->addRow(tr("Cell size"), pair(m_cellW, m_cellH, QStringLiteral("×"), this));
    form->addRow(tr("Offset"), pair(m_offsetX, m_offsetY, QStringLiteral(","), this));
    form->addRow(QString(), m_snap);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    const auto changed = [this] { emit settingsChanged(settings()); };
    for (QSpinBox *s : {m_cellW, m_cellH, m_offsetX, m_offsetY})
        connect(s, &QSpinBox::valueChanged, this, changed);
    connect(m_snap, &QCheckBox::toggled, this, changed);
}

GridSettings GridDialog::settings() const
{
    GridSettings g = m_base;
    g.cells = true; // editing the grid shows it
    g.cell = QSize(m_cellW->value(), m_cellH->value());
    g.offset = QPoint(m_offsetX->value(), m_offsetY->value());
    g.snap = m_snap->isChecked();
    return g;
}
