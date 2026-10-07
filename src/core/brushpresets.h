#pragma once

#include "brush.h"

#include <QImage>
#include <QList>
#include <QString>

namespace easeletch {

// A ready-made brush: a pencil, a pen, a stick of charcoal.
struct BrushPreset {
    QString id;    // stable, for settings
    QString name;  // as shown
    QString group; // as shown: the heading it's listed under
    BrushSettings settings;
};

// Built in, in the order they're listed: pencils, ink, charcoal and chalk,
// paint. The first of the paint ones, "round", is the plain brush Easeletch
// has always had.
const QList<BrushPreset> &brushPresets();
// Null if there's no such preset.
const BrushPreset *brushPreset(const QString &id);
inline QString defaultBrushPresetId()
{
    return QStringLiteral("round");
}

// A sample stroke made with the brush engine itself: pressed lightly, then
// hard, then lightly again, so what pressure does shows. The brush is drawn
// no bigger than the picture has room for.
QImage brushPreview(const BrushSettings &settings, const QSize &size, const QColor &ink = Qt::black,
                    const QColor &paper = Qt::white);

} // namespace easeletch
