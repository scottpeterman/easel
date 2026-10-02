#pragma once

#include "tilestore.h"

#include <QImage>
#include <QList>
#include <QPair>
#include <QSet>
#include <QSize>
#include <QString>

#include <vector>

namespace easeletch {

enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    SoftLight,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    Difference,
    Hue,
    Color,
};
inline constexpr int BlendModeCount = 12;

// Name used in .easeletch files ("normal", "soft-light", ...).
QString blendModeKey(BlendMode mode);
BlendMode blendModeFromKey(const QString &key, bool *ok = nullptr);

// Blends count source pixels over dst. Both are float RGBA, premultiplied,
// linear light. Coverage and Normal blending work in linear light; the other
// modes compare colours as sRGB values, so they match what other editors give
// (Overlay with 50% grey changes nothing, and so on).
void blendPixels(float *dst, const float *src, int count, float opacity, BlendMode mode);

// One entry in the stack: a raster layer, or a group holding other layers.
struct Layer {
    int id = 0;
    QString name;
    bool group = false;
    int parent = 0; // id of the group it's in; 0 = top level
    bool visible = true;
    bool locked = false;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    TileStore store; // raster layers only

    // A mask hides part of the layer (or group) without erasing it: where the
    // mask is white the layer shows, where it's black it doesn't, and greys
    // show it partly. Painted like any layer; what counts is how light the
    // mask is, in linear light, so black at 50% brush opacity hides half.
    bool hasMask = false;
    bool maskEnabled = true;
    TileStore mask{Qt::white};
};

// How much a mask pixel lets through, 0..1.
float maskValue(const Pixel &p);

// The document: layers bottom to top, the canvas size, and the flattened
// picture the canvas draws.
//
// Layers sit in one list; a layer's parent names the group it belongs to, and
// the order of siblings in the list is their stacking order. A group is
// composited on its own first, then blended as one layer.
//
// The composite is a TileStore kept up to date tile by tile: updateComposite()
// after painting, recompositeAll() after anything else. A tile only one opaque
// layer contributes to is shared with that layer, so a single-layer document
// costs no extra memory.
//
// Copies are cheap (tiles are shared), which is what undo snapshots rely on.
class LayerStack
{
public:
    LayerStack() = default;
    // A copy never shares its layer list with the original, so pointers to
    // the original's layers stay good while the copy is used elsewhere.
    LayerStack(const LayerStack &other);
    LayerStack &operator=(const LayerStack &other);
    LayerStack(LayerStack &&) = default;
    LayerStack &operator=(LayerStack &&) = default;
    // A document with one layer holding store.
    static LayerStack single(TileStore store, const QSize &size, const QString &name);

    QSize size() const { return m_size; }
    void setSize(const QSize &size) { m_size = size; }

    const QList<Layer> &layers() const { return m_layers; }
    int count() const { return int(m_layers.size()); }
    int indexOf(int id) const;
    Layer *layer(int id);
    const Layer *layer(int id) const;

    int activeId() const { return m_active; }
    void setActive(int id);
    Layer *active() { return layer(m_active); }
    const Layer *active() const { return layer(m_active); }

    // Children of a group (0 = top level), bottom to top.
    QList<int> children(int parent) const;
    // A group and everything inside it, or just the layer.
    QList<int> subtree(int id) const;
    bool isInside(int id, int group) const;
    // False when the layer or any group around it is hidden / locked.
    bool isShown(int id) const;
    bool isLocked(int id) const;
    QString uniqueName(const QString &base) const;

    // Inserts at a position among parent's children (0 = bottom; past the end
    // = top). A layer with id 0 gets a new id. Returns the id.
    int insert(Layer layer, int parent, int position);
    // Removes a layer, with everything inside it if it's a group.
    void remove(int id);
    // Moves a layer (with its contents) to a position among parent's children.
    // False if that would put a group inside itself.
    bool move(int id, int parent, int position);
    // Copies a layer (and its contents) just above it. Returns the copy's id.
    int duplicate(int id);
    // Blends a raster layer into the raster layer below it and removes it.
    // False if there's nothing suitable below.
    bool mergeDown(int id);
    // Replaces a group by one raster layer showing the same thing.
    bool mergeGroup(int id);
    // Gives a layer a mask that shows everything. False if it has one.
    bool addMask(int id);
    // Removes a layer's mask, leaving the layer whole again.
    bool removeMask(int id);
    // Erases what the mask hides and removes the mask (raster layers).
    bool applyMask(int id);
    // Replaces everything by one layer holding the composite.
    void flatten(const QString &name);
    // Applies a new order: every layer once, bottom to top, as (id, parent).
    // False (and nothing changes) if it isn't a valid arrangement.
    bool rearrange(const QList<QPair<int, int>> &order);
    int newId() { return m_nextId++; }
    void replaceLayers(QList<Layer> layers, int active);

    const TileStore &composite() const { return m_composite; }
    TileStore *compositeStore() { return &m_composite; }
    // Recomposites the tiles layers have changed since the last call.
    void updateComposite();
    void recompositeAll();
    // What a group's contents look like on their own.
    TileStore flattened(int group) const;

    qint64 memoryBytes() const;
    qsizetype tileCount() const;

    // A copy for undo: the layers, without the composite.
    LayerStack snapshot() const;
    // Exchanges layers, size and active layer with other; the composite stays
    // (call recompositeAll() afterwards).
    void swapState(LayerStack &other);
    // Bytes of tile data this copy holds that live doesn't share.
    qint64 bytesNotSharedWith(const LayerStack &live) const;

private:
    bool contributes(const Layer &l, TileCoord c) const;
    bool fetchLayer(const Layer &l, TileCoord c, std::vector<float> &out) const;
    bool compositeTile(int parent, TileCoord c, std::vector<float> &out) const;
    bool compositeDefault(int parent, float out[4]) const;
    QImage composedTile(TileCoord c) const;
    void recomposite(const QSet<TileCoord> &coords);
    QSet<TileCoord> tileCoordsUnder(int parent) const;

    QList<Layer> m_layers;
    QSize m_size;
    int m_active = 0;
    int m_nextId = 1;
    TileStore m_composite;
};

} // namespace easeletch
