#include "brushtool.h"

#include <QGuiApplication>
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
    b.grain = s.value(QStringLiteral("grain"), b.grain).toDouble();
    b.grainSize = s.value(QStringLiteral("grainSize"), b.grainSize).toDouble();
    b.minSize = s.value(QStringLiteral("minSize"), b.minSize).toDouble();
    b.jitter = s.value(QStringLiteral("jitter"), b.jitter).toDouble();
    const int tip = s.value(QStringLiteral("tip"), int(b.tip)).toInt();
    b.tip = tip >= int(easeletch::BrushTip::Round) && tip <= int(easeletch::BrushTip::Flat) ? easeletch::BrushTip(tip)
                                                                                           : easeletch::BrushTip::Round;
    b.aspect = s.value(QStringLiteral("aspect"), b.aspect).toDouble();
    b.followStroke = s.value(QStringLiteral("followStroke"), b.followStroke).toBool();
    b.angle = s.value(QStringLiteral("angle"), b.angle).toDouble();
    b.streaks = s.value(QStringLiteral("streaks"), b.streaks).toDouble();
    b.smear = s.value(QStringLiteral("smear"), b.smear).toDouble();
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
    s.setValue(QStringLiteral("grain"), b.grain);
    s.setValue(QStringLiteral("grainSize"), b.grainSize);
    s.setValue(QStringLiteral("minSize"), b.minSize);
    s.setValue(QStringLiteral("jitter"), b.jitter);
    s.setValue(QStringLiteral("tip"), int(b.tip));
    s.setValue(QStringLiteral("aspect"), b.aspect);
    s.setValue(QStringLiteral("followStroke"), b.followStroke);
    s.setValue(QStringLiteral("angle"), b.angle);
    s.setValue(QStringLiteral("streaks"), b.streaks);
    s.setValue(QStringLiteral("smear"), b.smear);
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
    const bool resized = bounds != m_bounds;
    m_bounds = bounds;
    m_history = history;
    if (resized) {
        m_hasLastEnd = false; // it was a place on another canvas
        emit symmetryChanged(); // a centred line moves with the canvas
    }
}

void BrushTool::setSymmetry(easeletch::Symmetry symmetry)
{
    if (symmetry == m_symmetry)
        return;
    m_symmetry = symmetry;
    emit symmetryChanged();
}

QPointF BrushTool::symmetryAxis() const
{
    return m_axisCentred ? QRectF(m_bounds).center() : m_axis;
}

void BrushTool::setSymmetryAxis(const QPointF &axis)
{
    if (!m_axisCentred && axis == m_axis)
        return;
    m_axis = axis;
    m_axisCentred = false;
    emit symmetryChanged();
}

void BrushTool::centreSymmetryAxis()
{
    if (m_axisCentred)
        return;
    m_axisCentred = true;
    emit symmetryChanged();
}

bool BrushTool::symmetryActive() const
{
    return m_symmetry != easeletch::Symmetry::Off && (m_mode == BrushMode::Paint || m_mode == BrushMode::Erase);
}

bool BrushTool::lastStrokeEnd(QPointF *pos) const
{
    if (m_hasLastEnd && pos)
        *pos = m_lastEnd;
    return m_hasLastEnd;
}

void BrushTool::setMode(BrushMode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged(mode);
    emit settingsChanged();
    emit symmetryChanged(); // it only applies to some modes
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

QString BrushTool::presetName() const
{
    const easeletch::BrushPreset *p = easeletch::brushPreset(m_preset);
    return p ? p->name : QString();
}

bool BrushTool::setPreset(const QString &id)
{
    const easeletch::BrushPreset *p = easeletch::brushPreset(id);
    if (!p)
        return false;
    m_preset = p->id;
    m_paint = p->settings;
    emit presetChanged(m_preset);
    emit settingsChanged();
    return true;
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
    // Only which brush it is: its settings are the ones just read, as they were left.
    const QString preset = s.value(QStringLiteral("brush/preset"), m_preset).toString();
    if (easeletch::brushPreset(preset) && preset != m_preset) {
        m_preset = preset;
        emit presetChanged(m_preset);
    }
    emit settingsChanged();
}

void BrushTool::saveSettings(QSettings &s) const
{
    save(s, QStringLiteral("brush"), m_paint);
    save(s, QStringLiteral("eraser"), m_erase);
    save(s, QStringLiteral("smudge"), m_smudge);
    save(s, QStringLiteral("clone"), m_clone);
    save(s, QStringLiteral("heal"), m_heal);
    s.setValue(QStringLiteral("brush/preset"), m_preset);
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
    m_stroke.setSymmetry(m_symmetry, symmetryAxis());
    // Shift: a straight line from where the last stroke ended to here.
    const bool line = m_hasLastEnd && QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    m_stroke.begin(m_store, m_bounds, settings(), m_color, m_mode,
                   line ? easeletch::StrokeSample{m_lastEnd, s.pressure} : s,
                   m_selection ? *m_selection : easeletch::Selection());
    if (line)
        m_stroke.moveTo(s);
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
    m_lastEnd = s.pos;
    m_hasLastEnd = true;
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
