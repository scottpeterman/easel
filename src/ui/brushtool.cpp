#include "brushtool.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

using easeletch::BrushMode;
using easeletch::BrushSettings;

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
    m_clone.size = 40.0;
    m_clone.hardness = 0.5;
    m_clone.pressureSize = false;
    m_heal.size = 30.0;
    m_heal.hardness = 0.7;
    m_heal.pressureSize = false;
}

void BrushTool::setCloneSource(const QPointF &pos)
{
    m_source = pos;
    m_hasSource = true;
    m_offsetFixed = false;
    emit cloneSourceChanged();
}

void BrushTool::setDocument(easeletch::TileStore *store, const QRect &bounds, easeletch::History *history, int layerId,
                            bool mask)
{
    m_layerId = layerId;
    m_layerMask = mask;
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
    case BrushMode::Clone:
        return m_clone;
    case BrushMode::Heal:
        return m_heal;
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
    load(s, QStringLiteral("clone"), m_clone);
    load(s, QStringLiteral("heal"), m_heal);
    emit settingsChanged();
}

void BrushTool::saveSettings(QSettings &s) const
{
    save(s, QStringLiteral("brush"), m_paint);
    save(s, QStringLiteral("eraser"), m_erase);
    save(s, QStringLiteral("smudge"), m_smudge);
    save(s, QStringLiteral("clone"), m_clone);
    save(s, QStringLiteral("heal"), m_heal);
}

void BrushTool::press(const easeletch::StrokeSample &s)
{
    if (!m_store) {
        emit blocked();
        return;
    }
    if (m_selection && !m_selection->isEmpty()
        && m_selection->coverage(int(std::floor(s.pos.x())), int(std::floor(s.pos.y()))) <= 0.0f)
        emit outsideSelection();
    if (m_mode == BrushMode::Clone) {
        if (!m_hasSource) {
            emit cloneSourceNeeded();
            return;
        }
        if (!m_offsetFixed) {
            m_offset = QPoint(int(std::lround(m_source.x() - s.pos.x())), int(std::lround(m_source.y() - s.pos.y())));
            m_offsetFixed = true;
        }
        // Where this stroke's copy starts, for the marker on the canvas.
        m_source = s.pos + QPointF(m_offset);
        emit cloneSourceChanged();
        m_stroke.setCloneOffset(m_offset);
    }
    m_stroke.begin(m_store, m_bounds, settings(), m_color, m_mode, s,
                   m_selection ? *m_selection : easeletch::Selection());
}

void BrushTool::move(const easeletch::StrokeSample &s)
{
    if (m_stroke.isActive())
        m_stroke.moveTo(s);
}

void BrushTool::release(const easeletch::StrokeSample &s)
{
    if (!m_stroke.isActive())
        return;
    m_stroke.moveTo(s);
    auto before = m_stroke.end();
    if (before.isEmpty() || !m_history)
        return;
    const QString label = m_mode == BrushMode::Erase    ? tr("Eraser")
                          : m_mode == BrushMode::Smudge ? tr("Smudge")
                          : m_mode == BrushMode::Clone  ? tr("Clone")
                          : m_mode == BrushMode::Heal   ? tr("Heal")
                                                        : tr("Brush");
    m_history->push(label, m_layerId, std::move(before), m_layerMask);
    emit strokeCommitted();
}

double BrushTool::cursorDiameter() const
{
    return settings().size;
}
