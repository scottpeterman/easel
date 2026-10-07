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
// square-ended bar for a flat brush or a knife.
enum class BrushTip { Round, Oval, Flat };

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
    // Mirrors the stroke about the lines through axis. Set before begin().
    // Paint and Erase only: the other modes work from what's under the
    // brush, which a mirror image of the stroke has no claim to.
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
    void paintOne(const StrokeSample &dab, const QPointF &wide);
    void paintDab(const StrokeSample &dab, const QPointF &wide);
    void smudgeDab(const StrokeSample &dab, const QPointF &wide);
    bool smears() const { return m_mode == BrushMode::Smudge || (m_mode == BrushMode::Paint && m_settings.smear > 0.0); }
    // Coverage of the tip at an offset from the dab's centre. wide: the unit
    // vector along the tip's wide side. radius: half its width.
    float tipCoverage(float dx, float dy, double radius, const QPointF &wide) const;
    void finishHeal();
    float coverageAt(double dist, double radius) const;

    TileStore *m_target = nullptr;
    TileStore m_before;
    QRect m_bounds;
    QRect m_canvas; // the whole canvas, whatever the selection
    QPoint m_cloneOffset;
    Symmetry m_symmetry = Symmetry::Off;
    QPointF m_axis;
    BrushSettings m_settings;
    BrushMode m_mode = BrushMode::Paint;
    float m_color[3] = {0, 0, 0}; // linear, straight alpha
    float m_colorAlpha = 1.0f;
    Stabilizer m_stabilizer;
    DabSpacer m_spacer;
    QHash<TileCoord, std::vector<float>> m_mask;
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
    quint32 m_strokeSeed = 0; // each stroke's bristles are its own
    quint32 m_jitterStep = 0; // one per dab on the line, so a mirrored stroke scatters the same way

    // Smudge: colour picked up under the brush, one premultiplied RGBA per
    // pixel offset from the dab centre.
    std::vector<std::array<float, 4>> m_carry;
    std::vector<quint8> m_carryLoaded; // per cell: picked up yet?
    int m_carryHalf = 0;
};

} // namespace easeletch
