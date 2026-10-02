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

    static constexpr double MinSize = 1.0;
    static constexpr double MaxSize = 1000.0;

    double diameterAt(double pressure) const;
    double dabStrengthAt(double pressure) const;
    double spacingAt(double pressure) const;
};

// Smudge drags and blends existing colour; opacity is its strength.
enum class BrushMode { Paint, Erase, Smudge };

struct StrokeSample {
    QPointF pos;
    double pressure = 1.0;
};

// Coverage (0..1) of a round dab at a distance from its centre. The rim is
// antialiased over one pixel; hardness < 1 adds a smooth falloff inside it.
float dabCoverage(double distance, double radius, double hardness);

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
    // Ends the stroke and returns the pre-stroke content of every tile it
    // touched (a null image = the tile didn't exist).
    QHash<TileCoord, QImage> end();

    bool isActive() const { return m_target != nullptr; }
    int dabCount() const { return m_dabCount; }

private:
    void paintDabs(const QList<StrokeSample> &dabs);
    void paintDab(const StrokeSample &dab);
    void smudgeDab(const StrokeSample &dab);
    float coverageAt(double dist, double radius) const;

    TileStore *m_target = nullptr;
    TileStore m_before;
    QRect m_bounds;
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

    // Smudge: colour picked up under the brush, one premultiplied RGBA per
    // pixel offset from the dab centre.
    std::vector<std::array<float, 4>> m_carry;
    std::vector<quint8> m_carryLoaded; // per cell: picked up yet?
    int m_carryHalf = 0;
};

} // namespace easeletch
