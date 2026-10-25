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
    const QString trees = QStringLiteral("Trees");
    const QString water = QStringLiteral("Watercolour");

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
        // Shaped tips that turn with the stroke, and bristles that show.
        make("flat", QStringLiteral("Flat brush"), paint,
             [](BrushSettings &b) {
                 b.size = 40, b.hardness = 0.9, b.opacity = 1.0, b.flow = 0.85, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.3, b.streaks = 0.45;
             }),
        make("filbert", QStringLiteral("Filbert"), paint,
             [](BrushSettings &b) {
                 b.size = 36, b.hardness = 0.6, b.opacity = 1.0, b.flow = 0.8, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.5, b.pressureOpacity = false;
                 b.tip = BrushTip::Oval, b.aspect = 0.45, b.streaks = 0.25;
             }),
        make("dry", QStringLiteral("Dry brush"), paint,
             [](BrushSettings &b) {
                 // Not enough paint to go round: bare streaks, and more of
                 // them the lighter the touch.
                 b.size = 44, b.hardness = 0.9, b.opacity = 0.95, b.flow = 0.85, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.7, b.pressureOpacity = true;
                 b.tip = BrushTip::Flat, b.aspect = 0.25, b.streaks = 0.85, b.grain = 0.25, b.grainSize = 2.0;
             }),
        make("knife", QStringLiteral("Palette knife"), paint,
             [](BrushSettings &b) {
                 // Lays nothing down: it pushes the paint that's there.
                 b.size = 50, b.hardness = 1.0, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.2;
                 b.pressureSize = false, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.12, b.smear = 0.85;
             }),
        make("fan", QStringLiteral("Fan brush"), paint,
             [](BrushSettings &b) {
                 // Tap it for a fir's boughs: each touch leaves an arc of
                 // bristle marks, drooping. Pull it for grass and branches.
                 b.size = 36, b.hardness = 0.85, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.2;
                 b.pressureSize = true, b.minSize = 0.45, b.pressureOpacity = false;
                 b.tip = BrushTip::Fan, b.aspect = 1.0, b.followStroke = false, b.angle = 0.0;
             }),
        make("knife-loaded", QStringLiteral("Loaded knife"), paint,
             [](BrushSettings &b) {
                 // Lays paint, but only where it catches: pulled lightly it
                 // skips in streaks the way it's going, as on a mountain's
                 // snow; pressed hard it covers.
                 b.size = 56, b.hardness = 1.0, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.35;
                 b.pressureSize = false, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.14, b.followStroke = true;
                 b.grain = 0.9, b.grainSize = 3.5, b.grainStretch = 7.0;
             }),
        make("airbrush", QStringLiteral("Airbrush"), paint,
             [](BrushSettings &b) {
                 b.size = 80, b.hardness = 0.0, b.opacity = 1.0, b.flow = 0.08, b.spacing = 0.06;
                 b.pressureSize = false, b.pressureOpacity = true;
             }),

        // Trees: the painter's round-brush leaf dabbing and knife-cut bark.
        // Foliage is built up dark to light: a pass in the shadow colour,
        // then the mid tone, then the lit leaves, each on its own layer.
        make("foliage", QStringLiteral("Foliage"), trees,
             [](BrushSettings &b) {
                 b.size = 60, b.hardness = 0.75, b.opacity = 1.0, b.flow = 0.95, b.spacing = 0.45;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = false;
                 b.tip = BrushTip::Leaf, b.aspect = 0.5, b.followStroke = false, b.angle = 90.0;
                 b.marks = 14, b.markSize = 0.22, b.sizeJitter = 0.5, b.angleJitter = 1.0;
                 b.valueJitter = 0.18, b.hueJitter = 0.12;
             }),
        make("foliage-far", QStringLiteral("Distant foliage"), trees,
             [](BrushSettings &b) {
                 // Fine speckle for a treeline or a far canopy.
                 b.size = 40, b.hardness = 0.5, b.opacity = 1.0, b.flow = 0.9, b.spacing = 0.4;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = false;
                 b.marks = 22, b.markSize = 0.1, b.sizeJitter = 0.6;
                 b.valueJitter = 0.12, b.hueJitter = 0.08;
             }),
        make("bark-left", QStringLiteral("Bark, light from left"), trees,
             [](BrushSettings &b) {
                 // A dark groove that tapers at both ends, with a thin lit
                 // edge beside it on the side the light comes from.
                 b.size = 9, b.hardness = 0.9, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.3, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.5, b.followStroke = true;
                 b.edge = 0.65, b.lightAngle = 180.0, b.edgeWidth = 0.3;
             }),
        make("bark-right", QStringLiteral("Bark, light from right"), trees,
             [](BrushSettings &b) {
                 b.size = 9, b.hardness = 0.9, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.3, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.5, b.followStroke = true;
                 b.edge = 0.65, b.lightAngle = 0.0, b.edgeWidth = 0.3;
             }),
        make("bark-ridges", QStringLiteral("Bark ridges"), trees,
             [](BrushSettings &b) {
                 // A broad plate of bark: lit across its face from the
                 // left, its sides torn and falling into the cracks, and
                 // fibres and dry streaks running the way it's pulled,
                 // round its curves. Pull long strokes up the trunk in the
                 // mid colour; the shading comes with them.
                 b.size = 30, b.hardness = 0.85, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.12;
                 b.pressureSize = true, b.minSize = 0.35, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.35, b.followStroke = true;
                 b.streaks = 0.35;
                 b.grain = 0.35, b.grainSize = 2.0, b.grainStretch = 9.0, b.rollGrain = true;
                 b.roughness = 0.6, b.relief = 0.6, b.lightAngle = 180.0;
             }),
        make("bark-texture", QStringLiteral("Bark texture"), trees,
             [](BrushSettings &b) {
                 // Plates of bark split by a net of dark fissures, as on a
                 // cottonwood or an old aspen. Lay the trunk in its shadow
                 // colour, then pull this up it in the plates' colour; the
                 // fissures come with it. The net is fixed to the canvas,
                 // running up and down, so strokes laid over each other
                 // join up into one trunk.
                 b.size = 70, b.hardness = 0.75, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.15;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = false;
                 b.roughness = 0.45;
                 b.plates = 1.0, b.plateSize = 16.0, b.plateStretch = 6.0, b.lightAngle = 180.0;
             }),
        make("bark-texture-curved", QStringLiteral("Bark texture, curving"), trees,
             [](BrushSettings &b) {
                 // The same net laid along the stroke and bending with it:
                 // for limbs, roots and a trunk that leans or twists.
                 b.size = 50, b.hardness = 0.75, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.12;
                 b.pressureSize = true, b.minSize = 0.5, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.4, b.followStroke = true;
                 b.roughness = 0.45, b.rollGrain = true;
                 b.plates = 1.0, b.plateSize = 13.0, b.plateStretch = 6.0, b.lightAngle = 180.0;
             }),
        make("bark-fissure", QStringLiteral("Fissure"), trees,
             [](BrushSettings &b) {
                 // A deep crack in the bark: dark, ragged-sided, thinning
                 // away to nothing at a light touch. Paint it in the darkest
                 // colour over the texture, up the trunk.
                 b.size = 7, b.hardness = 0.9, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.1;
                 b.pressureSize = true, b.minSize = 0.15, b.pressureOpacity = false;
                 b.roughness = 0.85, b.jitter = 0.04;
             }),

        // Watercolour: stains that darken what's under them rather than
        // covering it, dry with a darker edge, and settle in the paper.
        // Light colours first; each stroke over dry paint deepens it.
        make("wc-wash", QStringLiteral("Wash"), water,
             [](BrushSettings &b) {
                 // A broad, even, pale stain for skies and first layers.
                 b.size = 140, b.hardness = 0.35, b.opacity = 0.55, b.flow = 1.0, b.spacing = 0.08;
                 b.pressureSize = false, b.pressureOpacity = false;
                 b.watercolor = 0.5, b.granulation = 0.3, b.grainSize = 3.0;
                 b.wetness = 0.9, b.bleed = 45;
             }),
        make("wc-round", QStringLiteral("Round watercolour"), water,
             [](BrushSettings &b) {
                 b.size = 28, b.hardness = 0.7, b.opacity = 0.7, b.flow = 1.0, b.spacing = 0.08;
                 b.pressureSize = true, b.minSize = 0.25, b.pressureOpacity = true;
                 b.watercolor = 0.75, b.granulation = 0.4, b.grainSize = 2.6;
                 b.wetness = 0.5, b.bleed = 30;
             }),
        make("wc-flat", QStringLiteral("Flat watercolour"), water,
             [](BrushSettings &b) {
                 // Broad planes: a mountain's faces, a field, a band of trees.
                 b.size = 54, b.hardness = 0.75, b.opacity = 0.65, b.flow = 1.0, b.spacing = 0.1;
                 b.pressureSize = true, b.minSize = 0.6, b.pressureOpacity = false;
                 b.tip = BrushTip::Flat, b.aspect = 0.35, b.followStroke = true, b.streaks = 0.15;
                 b.watercolor = 0.7, b.granulation = 0.5, b.grainSize = 3.0;
                 b.wetness = 0.6, b.bleed = 35;
             }),
        make("wc-water", QStringLiteral("Clear water"), water,
             [](BrushSettings &b) {
                 // Wets the paper and lays no colour: paint laid into it
                 // afterwards runs and feathers. The paper dries in under a minute.
                 b.size = 120, b.hardness = 0.4, b.opacity = 1.0, b.flow = 1.0, b.spacing = 0.1;
                 b.pressureSize = false, b.pressureOpacity = false;
                 b.watercolor = 0.5, b.clearWater = true, b.wetness = 1.0;
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
    if (b.smear > 0.0) {
        // A knife shows nothing on bare paper: give it bands of paint to push.
        BrushSettings band;
        band.size = std::max(3.0, size.width() / 14.0);
        band.hardness = 1.0;
        band.pressureSize = false;
        for (int i = 1; i <= 4; ++i) {
            const double x = size.width() * (i / 5.0);
            stroke.begin(&store, bounds, band, ink, BrushMode::Paint, {{x, 2.0}, 1.0});
            stroke.moveTo({{x, size.height() - 2.0}, 1.0});
            stroke.end();
        }
    }
    WetPaper wet;
    if (b.clearWater)
        stroke.setWetPaper(&wet, 0.0);
    stroke.begin(&store, bounds, b, ink, BrushMode::Paint, sample(0.0));
    const int steps = std::max(16, size.width());
    for (int i = 1; i <= steps; ++i)
        stroke.moveTo(sample(double(i) / steps));
    stroke.end();
    if (b.clearWater) {
        // Water alone leaves nothing to see: show what it's for, a few drops
        // of colour running out into it.
        BrushSettings drop;
        drop.size = std::max(3.0, size.height() * 0.22);
        drop.hardness = 0.7, drop.pressureSize = false, drop.opacity = 0.8;
        drop.watercolor = 0.7, drop.bleed = size.height() * 0.35, drop.wetness = 0.5;
        for (int i = 0; i < 4; ++i) {
            BrushStroke dot;
            dot.setWetPaper(&wet, 0.0);
            const StrokeSample at = sample(0.2 + 0.2 * i);
            dot.begin(&store, bounds, drop, ink, BrushMode::Paint, {at.pos, 1.0});
            dot.moveTo({at.pos + QPointF(1.5, 0.5), 1.0});
            dot.end();
        }
    }

    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const TileCoord c : TileStore::tilesIntersecting(bounds))
        if (store.hasTile(c))
            painter.drawImage(TileStore::tileRect(c).topLeft(), TileStore::toDisplay(store.tile(c)));
    return out;
}

} // namespace easeletch
