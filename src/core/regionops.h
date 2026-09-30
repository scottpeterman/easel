#pragma once

#include "selection.h"
#include "tilestore.h"

#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSet>

namespace easel {

// Pixels of a selection as an image the size of its bounding box, in the tile
// format (RGBA16F, linear, premultiplied). Pixels outside the shape are transparent.
QImage extractSelection(const TileStore &store, const Selection &selection);

// Sets the selected pixels (within the canvas) to transparent. Returns the
// pre-change content of every tile it touched, for History.
QHash<TileCoord, QImage> clearSelection(TileStore &store, const Selection &selection, const QRect &canvas);

// Crops the canvas to rect (which must lie inside it): the rect's top-left
// becomes the origin and nothing outside it is kept. Returns the pre-crop
// content of every tile it touched, for History.
QHash<TileCoord, QImage> cropStore(TileStore &store, const QRect &rect);

// Magic wand: pixels whose colour is within tolerance (0..1, the largest
// difference in any 8-bit sRGB channel or alpha) of the pixel at seed.
// contiguous: only those connected to the seed (4-way); otherwise all over canvas.
Selection magicWand(const TileStore &store, const QRect &canvas, const QPoint &seed, double tolerance,
                    bool contiguous);

// Makes a colour transparent, the way GIMP's Color to Alpha does: each pixel
// becomes the most transparent version of itself that, over that colour, looks
// the same, so soft edges and glows against it keep their shape. Alpha left
// at or below threshold (0..1) becomes fully transparent, and the rest is
// stretched to keep full opacity. Works inside the selection, or everywhere
// on the canvas when it's empty. Returns the pre-change tiles, for History.
QHash<TileCoord, QImage> colorToAlpha(TileStore &store, const Selection &selection, const QRect &canvas,
                                      const QColor &color, double threshold);

// The smallest rectangle holding every pixel that isn't fully transparent
// (empty if there are none).
QRect opaqueBounds(const TileStore &store, const QRect &canvas);

// Conversions for the system clipboard: 8-bit sRGB with straight alpha.
QImage toClipboardImage(const QImage &content);
QImage fromClipboardImage(const QImage &image);

// Pixels floating above the canvas while you place them: a paste, or a
// selection lifted with the Move tool. The target store always shows the
// result, so the ordinary renderer draws it; the original tiles are kept, so
// cancel() restores them exactly and commit() yields one undo step covering the
// lift and the placement.
class FloatingContent
{
public:
    bool isActive() const { return m_target != nullptr; }

    // Cuts the selected pixels out of the target and floats them in place.
    void lift(TileStore *target, const Selection &selection, const QRect &canvas);
    // Floats content at a position. shape: its outline, relative to the content's top-left.
    void paste(TileStore *target, const QImage &content, const QPoint &position,
               const Selection &shape, const QRect &canvas);

    void moveTo(const QPoint &position);
    void moveBy(const QPoint &delta) { moveTo(m_position + delta); }

    QPoint position() const { return m_position; }
    QImage content() const { return m_content; }
    // The floating pixels' outline where they are now.
    Selection selection() const { return m_shape.translated(m_position); }
    bool wasLifted() const { return m_lifted; }

    // Leaves the content where it is. Returns the original content of every
    // tile touched since lift() / paste().
    QHash<TileCoord, QImage> commit();
    // Puts everything back as it was before lift() / paste().
    void cancel();

private:
    void place();
    void reset();

    TileStore *m_target = nullptr;
    TileStore m_original; // before lift / paste
    TileStore m_base;     // what the content floats over (with the hole, after a lift)
    QImage m_content;
    Selection m_shape;    // at the content's origin
    QPoint m_position;
    QRect m_canvas;
    QRect m_placed;       // canvas area covered at the current position
    QSet<TileCoord> m_touched;
    bool m_lifted = false;
};

} // namespace easel
