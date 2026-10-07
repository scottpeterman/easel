#include "brushpresets.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace easeletch {

const QList<BrushPreset> &brushPresets()
{
    const auto make = [](const char *id, const QString &name, const QString &group, auto &&set) {
        BrushPreset p;
        p.id = QString::fromLatin1(id);
        p.name = name;
        p.group = group;
        set(p.settings);
        return p;
    };
    const QString pencils = QStringLiteral("Pencils");
    const QString ink = QStringLiteral("Ink");
    const QString charcoal = QStringLiteral("Charcoal");
    const QString paint = QStringLiteral("Paint");

    // Sizes suit a canvas a couple of thousand pixels across.
    static const QList<BrushPreset> presets = {
        // Pencils: paper grain, and pressure that darkens more than it widens.
        make("pencil-hb", QStringLiteral("HB pencil"), pencils,
             [](BrushSettings &b) {
                 b.size = 4, b.hardness = 0.7, b.opacity = 0.85, b.flow = 0.45, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = true;
                 b.grain = 0.55, b.grainSize = 1.6;
             }),
        make("pencil-2b", QStringLiteral("2B pencil"), pencils,
             [](BrushSettings &b) {
                 b.size = 6, b.hardness = 0.6, b.opacity = 0.9, b.flow = 0.6, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.5, b.pressureOpacity = true;
                 b.grain = 0.5, b.grainSize = 1.8;
             }),
        make("pencil-6b", QStringLiteral("6B pencil"), pencils,
             [](BrushSettings &b) {
                 b.size = 11, b.hardness = 0.4, b.opacity = 0.95, b.flow = 0.7, b.spacing = 0.12;
                 b.pressureSize = true, b.minSize = 0.5, b.pressureOpacity = true;
                 b.grain = 0.6, b.grainSize = 2.0;
             }),
        make("pencil-mech", QStringLiteral("Mechanical"), pencils,
             [](BrushSettings &b) {
                 b.size = 2, b.hardness = 0.9, b.opacity = 0.9, b.flow = 0.8, b.spacing = 0.2;
                 b.pressureSize = true, b.minSize = 0.8, b.pressureOpacity = true;
                 b.grain = 0.25, b.grainSize = 1.2;
             }),

        // Ink: solid, hard-edged, a little steadied.
        make("ink-fine", QStringLiteral("Fineliner"), ink,
             [](BrushSettings &b) {
                 b.size = 3, b.hardness = 0.9, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.1;
                 b.pressureSize = false, b.pressureOpacity = false, b.stabilizer = 0.15;
             }),
        make("ink-tech", QStringLiteral("Tech pen"), ink,
             [](BrushSettings &b) {
                 b.size = 6, b.hardness = 0.95, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.08;
                 b.pressureSize = false, b.pressureOpacity = false, b.stabilizer = 0.15;
             }),
        make("ink-brush", QStringLiteral("Brush pen"), ink,
             [](BrushSettings &b) {
                 b.size = 18, b.hardness = 0.95, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.06;
                 b.pressureSize = true, b.minSize = 0.08, b.pressureOpacity = false, b.stabilizer = 0.2;
             }),
        make("ink-marker", QStringLiteral("Marker"), ink,
             [](BrushSettings &b) {
                 // One stroke is one even tone, however it crosses itself; a
                 // second stroke over it darkens, as a marker does.
                 b.size = 28, b.hardness = 0.85, b.opacity = 0.55, b.flow = 1.0, b.spacing = 0.08;
                 b.pressureSize = false, b.pressureOpacity = false;
             }),

        // Charcoal and chalk: coarse paper and a ragged edge.
        make("charcoal", QStringLiteral("Charcoal"), charcoal,
             [](BrushSettings &b) {
                 b.size = 26, b.hardness = 0.55, b.opacity = 0.9, b.flow = 0.5, b.spacing = 0.1;
                 b.pressureSize = true, b.minSize = 0.7, b.pressureOpacity = true;
                 b.grain = 0.8, b.grainSize = 2.6, b.jitter = 0.06;
             }),
        make("charcoal-soft", QStringLiteral("Soft charcoal"), charcoal,
             [](BrushSettings &b) {
                 b.size = 70, b.hardness = 0.15, b.opacity = 0.6, b.flow = 0.3, b.spacing = 0.1;
                 b.pressureSize = false, b.pressureOpacity = true;
                 b.grain = 0.7, b.grainSize = 3.0, b.jitter = 0.1;
             }),
        make("chalk", QStringLiteral("Chalk"), charcoal,
             [](BrushSettings &b) {
                 b.size = 20, b.hardness = 0.8, b.opacity = 0.95, b.flow = 0.8, b.spacing = 0.1;
                 b.pressureSize = true, b.minSize = 0.8, b.pressureOpacity = false;
                 b.grain = 0.65, b.grainSize = 2.2, b.jitter = 0.04;
             }),

        // Paint: smooth.
        make("round", QStringLiteral("Round brush"), paint, [](BrushSettings &) {}),
        make("airbrush", QStringLiteral("Airbrush"), paint,
             [](BrushSettings &b) {
                 b.size = 80, b.hardness = 0.0, b.opacity = 1.0, b.flow = 0.08, b.spacing = 0.06;
                 b.pressureSize = false, b.pressureOpacity = true;
             }),
    };
    return presets;
}

const BrushPreset *brushPreset(const QString &id)
{
    for (const BrushPreset &p : brushPresets())
        if (p.id == id)
            return &p;
    return nullptr;
}

QImage brushPreview(const BrushSettings &settings, const QSize &size, const QColor &ink, const QColor &paper)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(paper);
    if (size.width() < 8 || size.height() < 8)
        return out;

    BrushSettings b = settings;
    b.size = std::min(b.size, size.height() * 0.45);
    b.stabilizer = 0.0; // the sample is already a smooth curve
    const double margin = std::max(3.0, b.size * 0.5 + 2.0);
    const double w = size.width() - 2.0 * margin, mid = size.height() * 0.5;
    const double swing = std::max(0.0, mid - margin);

    // One lazy S across the picture, the pressure rising to full in the
    // middle and falling away again.
    const auto sample = [&](double t) {
        const double pressure = 0.15 + 0.85 * std::sin(t * 3.141592653589793);
        return StrokeSample{QPointF(margin + w * t, mid - swing * std::sin(t * 6.283185307179586)), pressure};
    };
    TileStore store(paper);
    const QRect bounds(QPoint(0, 0), size);
    BrushStroke stroke;
    stroke.begin(&store, bounds, b, ink, BrushMode::Paint, sample(0.0));
    const int steps = std::max(16, size.width());
    for (int i = 1; i <= steps; ++i)
        stroke.moveTo(sample(double(i) / steps));
    stroke.end();

    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const TileCoord c : TileStore::tilesIntersecting(bounds))
        if (store.hasTile(c))
            painter.drawImage(TileStore::tileRect(c).topLeft(), TileStore::toDisplay(store.tile(c)));
    return out;
}

} // namespace easeletch
