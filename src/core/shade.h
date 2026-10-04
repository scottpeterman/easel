#pragma once

#include "fillops.h"
#include "selection.h"
#include "tilestore.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QRect>

#include <vector>

namespace easeletch {

// Shade Areas: every enclosed area of a line drawing gets a gradient of its
// own, all running the same way, so a drawing's panels can be shaded as metal
// in one go and not one fill at a time.
struct ShadeSettings {
    // The colours across each area, from the side the light comes from.
    GradientStops stops = softShadeStops();
    // Colours the gradient (red for red metal); invalid leaves it as it is.
    // The brightest highlights stay near white, which is what reads as metal.
    QColor tint;
    // The way each gradient runs, in degrees clockwise: 0 is left to right,
    // 90 top to bottom.
    double angle = 55.0;
    // Lighter (to 1) or darker (to -1) overall: a face turned to the light,
    // or away from it.
    double brightness = 0.0;
    // How much areas differ from one another in tone (0..1), so neighbouring
    // panels don't merge into one sheet.
    double variation = 0.1;

    // Light at the start, in shade at the end: a flat surface in even light.
    static GradientStops softShadeStops();
    ShadeSettings normalized() const;
    // The stops with tint, brightness and one area's share of the variation
    // (shift: -0.5..0.5) worked in.
    GradientStops stopsFor(double shift) const;
};

// Which areas to shade.
struct AreaOptions {
    // Areas smaller than this many pixels are left alone (rivets, the gaps in
    // lettering, specks).
    int minArea = 200;
    // Only areas that are still white or empty. Off: any area that isn't dark.
    bool whiteOnly = true;
    // How far the shading reaches under the lines around each area, in pixels,
    // so no pale fringe is left along them.
    int grow = 3;
};

// Finds the areas once, then shades them as often as the settings change.
class AreaShader
{
public:
    // The areas of `lineArt`: runs of joined pixels close in colour to one
    // another that are light enough to shade. An area that touches the edge
    // of the canvas is the page around the drawing, and is left out. With a
    // selection, an area counts if most of it is selected, so a loose lasso
    // round a group of panels picks exactly those panels.
    void findAreas(const TileStore &lineArt, const Selection &within, const QRect &canvas, const AreaOptions &options);
    int areaCount() const { return int(m_areas.size()); }
    bool isEmpty() const { return m_areas.empty(); }
    // The area a pixel belongs to (the lines round it included, as far as
    // `grow` reaches), or -1.
    int areaAt(int x, int y) const;

    // Lays each area's gradient on target, replacing what was there. Returns
    // the pre-change content of every tile it changed, for History.
    QHash<TileCoord, QImage> shade(TileStore &target, const ShadeSettings &settings) const;

private:
    struct Area {
        QRect box;
    };
    QRect m_rect;
    std::vector<int> m_owner; // per pixel of m_rect: its area, or -1
    std::vector<Area> m_areas;
};

} // namespace easeletch
