#pragma once

#include "selection.h"
#include "tilestore.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>

#include <QSet>

#include <array>
#include <deque>
#include <vector>

namespace easeletch {

// The shape of the brush's tip. Oval and Flat are as wide as the size and
// BrushSettings::aspect times that thick: an oval for a filbert, a
// square-ended bar for a flat brush or a knife. Leaf is pointed at both
// ends, as long as the size and aspect times that across at its widest.
// Fan is a fan brush: a spread of bristles whose ragged tips leave an arc of
// separate marks, sagging away from the handle (down, held level).
enum class BrushTip { Round, Oval, Flat, Leaf, Fan };

struct BrushSettings {
    double size = 24.0;      // diameter in canvas pixels at full pressure
    double hardness = 0.8;   // 0 = soft falloff from the centre, 1 = hard edge
    double opacity = 1.0;    // ceiling on coverage within one stroke
    double flow = 1.0;       // strength of each dab; overlapping dabs build up to opacity
    double spacing = 0.12;   // distance between dabs, as a fraction of the diameter
    double stabilizer = 0.0; // 0 = raw input, 1 = heavy smoothing
    bool pressureSize = true;
    bool pressureOpacity = false;
    // Hard, unantialiased dabs centred on pixels: a 1 px brush sets exactly one
    // pixel. For sprites and pixel art; hardness is ignored.
    bool pixel = false;
    // Paper. 0 is smooth; towards 1 only the high points of the paper take
    // colour at a light touch, and pressing harder reaches into the hollows:
    // what makes a mark read as pencil or charcoal rather than paint.
    double grain = 0.0;
    double grainSize = 2.0; // across one bump of the paper, in canvas pixels
    // Above 1, the paper's bumps are this many times longer along the stroke
    // than across it: a loaded knife skipping over the canvas leaves broken
    // streaks that run the way it was pulled.
    double grainStretch = 1.0;
    // With pressure size: how much of the diameter is left at the lightest
    // touch. 0 tapers to nothing (a brush pen); 0.7 hardly changes (a pencil).
    double minSize = 0.0;
    // Each dab lands up to this fraction of the diameter off the line, for the
    // ragged edge of a crumbling stick.
    double jitter = 0.0;

    BrushTip tip = BrushTip::Round;
    double aspect = 1.0; // Oval and Flat: how thick the tip is for its width
    // Oval and Flat: the tip turns to stay square to the way the stroke is
    // going, so it always paints at its full width, as a brush drawn along
    // does. Off, it's held at `angle` (degrees, of its wide side) and paints
    // broad one way and thin the other, as a pen nib does.
    bool followStroke = true;
    double angle = 0.0;
    // Bristles: lines of more and less paint running along the stroke. 0 is
    // an even coat; towards 1, and with a lighter touch, bare gaps open up.
    double streaks = 0.0;
    // Brush mode only: above 0 the stroke lays no paint. It drags what is
    // already there, this strongly, as a palette knife does.
    double smear = 0.0;

    // Foliage: above 1, every dab is a cluster of this many marks scattered
    // over the brush, each markSize times its diameter. More of them the
    // harder it's pressed. The tip shapes each mark.
    int marks = 1;
    double markSize = 1.0;
    double sizeJitter = 0.0;  // each mark up to this much smaller
    double angleJitter = 0.0; // each mark turned up to this fraction of a half turn either way
    // Each mark's colour wanders from the painting colour: lighter or darker
    // by up to this much, and its hue by up to a twelfth of the way round.
    double valueJitter = 0.0;
    double hueJitter = 0.0;

    // Bark: above 0, a thin lighter line runs beside the stroke on the side
    // the light comes from, this much of the way to white. lightAngle is
    // where the light comes from, in degrees (0 from the right, 90 from
    // below, 180 from the left, 270 from above).
    double edge = 0.0;
    double lightAngle = 180.0;
    double edgeWidth = 0.3; // across the edge, as a fraction of the tip's width

    // Torn edge: above 0, the sides of the stroke wander in and out by up to
    // this much of its half-width, in stringy runs along it, as split bark
    // and dry paint do. Both sides tear their own way.
    double roughness = 0.0;
    // Grain that rides the stroke: the paper's bumps, and with grainStretch
    // its streaks, are laid out along the path the stroke takes, so they bend
    // round its curves and stay parallel to its sides. Off, they're fixed to
    // the canvas (or, stretched, to the way the stroke set off).
    bool rollGrain = false;
    // Relief: above 0, each stroke is lit across its width from lightAngle,
    // lifted towards the light and sunk away from it by this much, with
    // fibres running along it: a ridge of bark rather than a flat band.
    double relief = 0.0;
    // Bark plates: above 0, the stroke lays a net of plates split by dark
    // fissures, this strongly. Each plate is lit on the side facing
    // lightAngle and has its own shade. plateSize is across one, in pixels;
    // plateStretch how many times longer than wide. With rollGrain the net
    // runs along the stroke and bends with it; without, it's fixed to the
    // canvas, running up and down, so overlapping strokes share one net.
    double plates = 0.0;
    double plateSize = 14.0;
    double plateStretch = 3.5;

    // Watercolour: above 0, the paint is a stain, not a cover. It darkens
    // what's under it by its colour, as a glaze does (on an empty layer it
    // lays down as usual), so it never lightens and going over dry paint
    // deepens it. When the stroke ends it dries: the pigment drifts to the
    // edge, which comes out darker by this much.
    double watercolor = 0.0;
    // Watercolour: pigment settles in the paper's hollows and thins on its
    // high points, by this much (0..1), in bumps grainSize across.
    double granulation = 0.0;
    // Watercolour on wet paper: how much water a stroke leaves behind (0..1),
    // and how far, in pixels, paint runs into paper that's still wet.
    double wetness = 0.0;
    double bleed = 0.0;
    // Watercolour: clear water. Lays no colour, only wets the paper, so the
    // next strokes laid into it bleed.
    bool clearWater = false;

    static constexpr double MinSize = 1.0;
    static constexpr double MaxSize = 1000.0;

    double diameterAt(double pressure) const;
    double dabStrengthAt(double pressure) const;
    double spacingAt(double pressure) const;
};

// Smudge drags and blends existing colour; opacity is its strength.
// Clone paints with a copy of the layer taken from somewhere else on it (see
// BrushStroke::setCloneOffset). Heal marks what the stroke covers and, when
// the stroke ends, replaces it with what's around it (see heal.h).
enum class BrushMode { Paint, Erase, Smudge, Clone, Heal };

// Painting mirrored as it's made: every dab is repeated across a line through
// a point on the canvas. LeftRight mirrors across an upright line, TopBottom
// across a level one, Quarters across both (four strokes for one).
enum class Symmetry { Off, LeftRight, TopBottom, Quarters };

struct StrokeSample {
    QPointF pos;
    double pressure = 1.0;
};

// Coverage (0..1) of a round dab at a distance from its centre. The rim is
// antialiased over one pixel; hardness < 1 adds a smooth falloff inside it.
float dabCoverage(double distance, double radius, double hardness);

// The height of the paper at a canvas pixel, 0 (a hollow) to 1 (a high
// point). It belongs to the canvas, not the stroke: going over the same place
// again finds the same bumps, as it does on paper. size: across one bump.
float paperTooth(int x, int y, double size);
// Smooth noise, 0..1, with bumps scaleX by scaleY pixels across. salt picks
// one of any number of unrelated patterns. Fixed to the canvas, as the tooth is.
float smoothNoise(int x, int y, double scaleX, double scaleY, int salt);

// Moving average over the last few input samples. Strength 0 passes input
// straight through; 1 averages over 32 samples.
class Stabilizer
{
public:
    void setStrength(double strength);
    int window() const { return m_window; }

    void reset(const StrokeSample &first);
    StrokeSample add(const StrokeSample &raw);
    // Walks the smoothed point the rest of the way to the last raw sample, so a
    // stroke ends where the pen lifted.
    QList<StrokeSample> flush();

private:
    int m_window = 1;
    std::deque<StrokeSample> m_samples;
};

// Places dabs at even spacing along the stroke path, carrying the leftover
// distance from one segment to the next.
class DabSpacer
{
public:
    void begin(const StrokeSample &first, QList<StrokeSample> &dabs);
    void moveTo(const StrokeSample &to, const BrushSettings &settings, QList<StrokeSample> &dabs);

private:
    StrokeSample m_last;
    double m_sinceLastDab = 0.0;
};

// Renders one stroke into a tile store.
//
// Dabs accumulate into a per-stroke coverage mask (flow builds up, capped at 1),
// and every touched pixel is recomposited from its pre-stroke value with
// coverage x opacity. So overlapping dabs within one stroke never exceed the
// opacity setting, while a new stroke over the old one builds up as expected.
// Smudge instead drags colour along the stroke (see smudgeDab). Painting is
// clipped to the canvas bounds and to the selection, when there is one.
// Where a layer's paper is still wet from watercolour, and how wet (0..1).
// Water fades over DryTime seconds; times are seconds on any steady clock.
class WetPaper
{
public:
    static constexpr double DryTime = 45.0;

    // Wetness over area at time now, row by row into out (resized to fit).
    void sample(const QRect &area, double now, std::vector<float> &out) const;
    float wetnessAt(int x, int y, double now) const;
    // Wets area (amount row by row, as from sample) at time now: each place
    // keeps the wetter of what it had left and what it's given.
    void wet(const QRect &area, const std::vector<float> &amount, double now);
    // Forgets paper that has dried.
    void prune(double now);
    void clear() { m_tiles.clear(); }
    bool isEmpty() const { return m_tiles.isEmpty(); }

private:
    struct Tile {
        std::vector<float> wet;
        double time = 0.0;
    };
    static float left(double age);
    QHash<TileCoord, Tile> m_tiles;
};

class BrushStroke
{
public:
    // clip: painting stays inside it when it isn't empty.
    void begin(TileStore *target, const QRect &bounds, const BrushSettings &settings,
               const QColor &color, BrushMode mode, const StrokeSample &first,
               const Selection &clip = {});
    void moveTo(const StrokeSample &raw);
    // Clone: where each painted pixel is copied from, relative to itself. Set
    // before begin(). The copy is of the layer as it was when the stroke
    // began, so a stroke never copies its own paint.
    void setCloneOffset(const QPoint &offset) { m_cloneOffset = offset; }
    // Erase: what erasing leaves behind. Transparent unless set; a page's
    // solid background erases back to its own colour, as paper does. Set
    // before begin().
    void setEraseTo(const Pixel &p) { m_eraseTo = p; }
    void clearEraseTo() { m_eraseTo = Pixel{}; }
    // Mirrors the stroke about the lines through axis. Set before begin().
    // Paint and Erase only: the other modes work from what's under the
    // brush, which a mirror image of the stroke has no claim to.
    // Watercolour strokes wet this paper, and bleed where it's already wet.
    // now: the time, on the clock the paper's times are on. Set before begin().
    void setWetPaper(WetPaper *paper, double now)
    {
        m_wet = paper;
        m_now = now;
    }
    void setSymmetry(Symmetry symmetry, const QPointF &axis)
    {
        m_symmetry = symmetry;
        m_axis = axis;
    }
    // Ends the stroke and returns the pre-stroke content of every tile it
    // touched (a null image = the tile didn't exist).
    QHash<TileCoord, QImage> end();

    bool isActive() const { return m_target != nullptr; }
    int dabCount() const { return m_dabCount; }

private:
    void paintDabs(const QList<StrokeSample> &dabs);
    // One mark of paint: where, how big, which way, what colour.
    struct Mark {
        QPointF pos;
        double pressure = 1.0;
        double radius = 0.5;
        QPointF wide;
        float color[3] = {0, 0, 0}; // linear
        // Where the mark is on the stroke: how far along it, which way it's
        // going there (a unit vector), and how far off its middle (pixels,
        // along the stroke's left hand). For grain that rides the stroke, the
        // torn edge and relief.
        double along = 0.0;
        QPointF heading{1.0, 0.0};
        double offset = 0.0;
    };
    // along: how far along the stroke the dab is. offset: how far it was
    // jittered off the stroke's middle, to its left.
    void paintOne(const StrokeSample &dab, const QPointF &wide, double along = 0.0, double offset = 0.0);
    void marksFor(const StrokeSample &dab, const QPointF &wide, double along, double offset, QList<Mark> &out);
    // The way the stroke is going here, as a unit vector.
    QPointF headingFor(const QPointF &wide) const;
    void paintMark(const Mark &mark);
    void shadeColor(double dv, double dh, double desaturate, float out[3]) const;
    void smudgeDab(const StrokeSample &dab, const QPointF &wide);
    bool smears() const { return m_mode == BrushMode::Smudge || (m_mode == BrushMode::Paint && m_settings.smear > 0.0); }
    // Coverage of the tip at an offset from the dab's centre. wide: the unit
    // vector along the tip's wide side. radius: half its width.
    // grow: pixels to push the edge out by (in, below 0), for a torn edge.
    float tipCoverage(float dx, float dy, double radius, const QPointF &wide, float grow = 0.0f) const;
    // How far the torn edge reaches out at a place on the stroke, in pixels:
    // along is the distance along it, side which side (+1 left, -1 right),
    // halfWidth the stroke's half-width there.
    float tear(double along, int side, double halfWidth) const;
    // Bark plates at a point (px across the net, py along it): how much
    // lighter (+) or darker (-) the paint is there. lx, ly: the direction
    // the light comes from, in the same terms.
    float plateShade(float px, float py, float lx, float ly) const;
    void finishHeal();
    void finishWatercolor();
    // Laying a stain of colour col (linear) at strength k over d.
    static Pixel stain(const Pixel &d, const float col[3], float k);
    float granuleAt(int x, int y) const;
    // granuleAt for every pixel of a tile, worked out once per stroke.
    const float *granules(TileCoord c);
    float coverageAt(double dist, double radius) const;
    float fanCoverage(float u, float v, float a) const;

    TileStore *m_target = nullptr;
    TileStore m_before;
    QRect m_bounds;
    QRect m_canvas; // the whole canvas, whatever the selection
    QPoint m_cloneOffset;
    Pixel m_eraseTo{};
    Symmetry m_symmetry = Symmetry::Off;
    QPointF m_axis;
    BrushSettings m_settings;
    BrushMode m_mode = BrushMode::Paint;
    float m_color[3] = {0, 0, 0}; // linear, straight alpha
    float m_colorAlpha = 1.0f;
    Stabilizer m_stabilizer;
    DabSpacer m_spacer;
    QHash<TileCoord, std::vector<float>> m_mask;
    // Strokes whose marks differ in colour keep, beside the coverage, the
    // colour laid so far (linear, premultiplied by that coverage).
    bool m_multiColor = false;
    bool m_watercolor = false;
    WetPaper *m_wet = nullptr;
    double m_now = 0.0;
    QHash<TileCoord, std::vector<float>> m_paint;
    QHash<TileCoord, std::vector<float>> m_granules;
    QColor m_baseColor;
    QSet<TileCoord> m_touched;
    Selection m_clip;
    int m_dabCount = 0;
    // Which way the stroke is going, for tips that turn with it and for
    // bristle streaks. Unknown until it has moved: the first dab waits for it.
    bool m_needsHeading = false;
    bool m_hasHeading = false;
    QPointF m_heading;
    QPointF m_headingFrom;
    bool m_hasPending = false;
    StrokeSample m_pending;
    double m_pendingAlong = 0.0;
    quint32 m_strokeSeed = 0; // each stroke's bristles are its own
    quint32 m_jitterStep = 0; // one per dab on the line, so a mirrored stroke scatters the same way
    // Stretched grain runs one way for the whole stroke, from where it began:
    // turning it with every bend would make the streaks swirl.
    bool m_hasGrainAxis = false;
    QPointF m_grainAxis;
    QPointF m_grainOrigin;
    // How far along its path the stroke has come, at the dab being painted.
    double m_along = 0.0;
    bool m_hasLastDab = false;
    QPointF m_lastDab;

    // Smudge: colour picked up under the brush, one premultiplied RGBA per
    // pixel offset from the dab centre.
    std::vector<std::array<float, 4>> m_carry;
    std::vector<quint8> m_carryLoaded; // per cell: picked up yet?
    int m_carryHalf = 0;
};

} // namespace easeletch
