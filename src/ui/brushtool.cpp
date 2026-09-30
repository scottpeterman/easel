#include "brushtool.h"

#include <QSettings>

#include <algorithm>

using easel::BrushMode;
using easel::BrushSettings;

namespace {

void load(QSettings &s, const QString &group, BrushSettings &b)
{
    s.beginGroup(group);
    b.size = s.value(QStringLiteral("size"), b.size).toDouble();
    b.hardness = s.value(QStringLiteral("hardness"), b.hardness).toDouble();
    b.opacity = s.value(QStringLiteral("opacity"), b.opacity).toDouble();
    b.flow = s.value(QStringLiteral("flow"), b.flow).toDouble();
    b.spacing = s.value(QStringLiteral("spacing"), b.spacing).toDouble();
    b.stabilizer = s.value(QStringLiteral("stabilizer"), b.stabilizer).toDouble();
    b.pressureSize = s.value(QStringLiteral("pressureSize"), b.pressureSize).toBool();
    b.pressureOpacity = s.value(QStringLiteral("pressureOpacity"), b.pressureOpacity).toBool();
    b.pixel = s.value(QStringLiteral("pixel"), b.pixel).toBool();
    s.endGroup();
}

void save(QSettings &s, const QString &group, const BrushSettings &b)
{
    s.beginGroup(group);
    s.setValue(QStringLiteral("size"), b.size);
    s.setValue(QStringLiteral("hardness"), b.hardness);
    s.setValue(QStringLiteral("opacity"), b.opacity);
    s.setValue(QStringLiteral("flow"), b.flow);
    s.setValue(QStringLiteral("spacing"), b.spacing);
    s.setValue(QStringLiteral("stabilizer"), b.stabilizer);
    s.setValue(QStringLiteral("pressureSize"), b.pressureSize);
    s.setValue(QStringLiteral("pressureOpacity"), b.pressureOpacity);
    s.setValue(QStringLiteral("pixel"), b.pixel);
    s.endGroup();
}

} // namespace

BrushTool::BrushTool(QObject *parent)
    : QObject(parent)
{
    m_erase.size = 40.0;
    m_erase.hardness = 0.9;
    m_smudge.size = 40.0;
    m_smudge.hardness = 0.5;
    m_smudge.opacity = 0.6; // strength
    m_smudge.spacing = 0.08;
}

void BrushTool::setDocument(easel::TileStore *store, const QRect &bounds, easel::History *history)
{
    if (m_stroke.isActive())
        m_stroke.end(); // the old document is going away; nothing to record
    m_store = store;
    m_bounds = bounds;
    m_history = history;
}

void BrushTool::setMode(BrushMode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged(mode);
    emit settingsChanged();
}

BrushSettings &BrushTool::current()
{
    switch (m_mode) {
    case BrushMode::Erase:
        return m_erase;
    case BrushMode::Smudge:
        return m_smudge;
    case BrushMode::Paint:
        break;
    }
    return m_paint;
}

BrushSettings BrushTool::settings() const
{
    return const_cast<BrushTool *>(this)->current();
}

void BrushTool::setSettings(const BrushSettings &settings)
{
    current() = settings;
    current().size = std::clamp(settings.size, BrushSettings::MinSize, BrushSettings::MaxSize);
    emit settingsChanged();
}

void BrushTool::scaleSize(double factor)
{
    BrushSettings b = settings();
    b.size = std::round(b.size * factor);
    // Always move at least one pixel so small sizes still change.
    if (factor > 1.0 && b.size <= settings().size)
        b.size = settings().size + 1.0;
    if (factor < 1.0 && b.size >= settings().size)
        b.size = settings().size - 1.0;
    setSettings(b);
}

void BrushTool::loadSettings(QSettings &s)
{
    load(s, QStringLiteral("brush"), m_paint);
    load(s, QStringLiteral("eraser"), m_erase);
    load(s, QStringLiteral("smudge"), m_smudge);
    emit settingsChanged();
}

void BrushTool::saveSettings(QSettings &s) const
{
    save(s, QStringLiteral("brush"), m_paint);
    save(s, QStringLiteral("eraser"), m_erase);
    save(s, QStringLiteral("smudge"), m_smudge);
}

void BrushTool::press(const easel::StrokeSample &s)
{
    if (!m_store)
        return;
    m_stroke.begin(m_store, m_bounds, settings(), m_color, m_mode, s,
                   m_selection ? *m_selection : easel::Selection());
}

void BrushTool::move(const easel::StrokeSample &s)
{
    if (m_stroke.isActive())
        m_stroke.moveTo(s);
}

void BrushTool::release(const easel::StrokeSample &s)
{
    if (!m_stroke.isActive())
        return;
    m_stroke.moveTo(s);
    auto before = m_stroke.end();
    if (before.isEmpty() || !m_history)
        return;
    const QString label = m_mode == BrushMode::Erase    ? tr("Eraser")
                          : m_mode == BrushMode::Smudge ? tr("Smudge")
                                                        : tr("Brush");
    m_history->push(label, std::move(before));
    emit strokeCommitted();
}

double BrushTool::cursorDiameter() const
{
    return settings().size;
}
