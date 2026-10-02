#include "layerstack.h"

#include <QFloat16>
#include <QHash>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace easeletch {

namespace {

constexpr int kTilePixels = TileStore::TileSize * TileStore::TileSize;

struct BlendKey {
    BlendMode mode;
    const char *key;
};
constexpr BlendKey kBlendKeys[BlendModeCount] = {
    {BlendMode::Normal, "normal"},          {BlendMode::Multiply, "multiply"},
    {BlendMode::Screen, "screen"},          {BlendMode::Overlay, "overlay"},
    {BlendMode::SoftLight, "soft-light"},   {BlendMode::Darken, "darken"},
    {BlendMode::Lighten, "lighten"},        {BlendMode::ColorDodge, "color-dodge"},
    {BlendMode::ColorBurn, "color-burn"},   {BlendMode::Difference, "difference"},
    {BlendMode::Hue, "hue"},                {BlendMode::Color, "color"},
};

// sRGB <-> linear for values in 0..1, by table with linear interpolation.
// Encoding is steep near black, so that table is indexed by the square root.
constexpr int kLutSize = 4096;

struct Luts {
    std::array<float, kLutSize + 2> encode; // index: sqrt(linear) * kLutSize
    std::array<float, kLutSize + 2> decode; // index: srgb * kLutSize
    Luts()
    {
        for (int i = 0; i < kLutSize + 2; ++i) {
            const float t = std::min(1.0f, float(i) / float(kLutSize));
            encode[size_t(i)] = linearToSrgb(t * t);
            decode[size_t(i)] = srgbToLinear(t);
        }
    }
};

const Luts &luts()
{
    static const Luts l;
    return l;
}

inline float lookup(const std::array<float, kLutSize + 2> &table, float t)
{
    const float f = t * float(kLutSize);
    const int i = int(f);
    const float frac = f - float(i);
    return table[size_t(i)] + (table[size_t(i) + 1] - table[size_t(i)]) * frac;
}

inline float encode(const Luts &l, float linear)
{
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return lookup(l.encode, std::sqrt(c));
}

inline float decode(const Luts &l, float srgb)
{
    return lookup(l.decode, std::clamp(srgb, 0.0f, 1.0f));
}

inline float lum(const float c[3])
{
    return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
}

inline void clipColor(float c[3])
{
    const float l = lum(c);
    const float n = std::min({c[0], c[1], c[2]});
    const float x = std::max({c[0], c[1], c[2]});
    if (n < 0.0f) {
        for (int k = 0; k < 3; ++k)
            c[k] = l + (c[k] - l) * l / (l - n);
    }
    if (x > 1.0f) {
        for (int k = 0; k < 3; ++k)
            c[k] = l + (c[k] - l) * (1.0f - l) / (x - l);
    }
}

inline void setLum(float c[3], float l)
{
    const float d = l - lum(c);
    for (int k = 0; k < 3; ++k)
        c[k] += d;
    clipColor(c);
}

inline float sat(const float c[3])
{
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
}

inline void setSat(float c[3], float s)
{
    int lo = 0, mid = 1, hi = 2;
    if (c[lo] > c[mid])
        std::swap(lo, mid);
    if (c[mid] > c[hi])
        std::swap(mid, hi);
    if (c[lo] > c[mid])
        std::swap(lo, mid);
    if (c[hi] > c[lo]) {
        c[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo]);
        c[hi] = s;
    } else {
        c[mid] = 0.0f;
        c[hi] = 0.0f;
    }
    c[lo] = 0.0f;
}

inline float blendChannel(BlendMode mode, float b, float s)
{
    switch (mode) {
    case BlendMode::Multiply:
        return b * s;
    case BlendMode::Screen:
        return b + s - b * s;
    case BlendMode::Overlay:
        return b <= 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - b) * (1.0f - s);
    case BlendMode::SoftLight: {
        if (s <= 0.5f)
            return b - (1.0f - 2.0f * s) * b * (1.0f - b);
        const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : std::sqrt(b);
        return b + (2.0f * s - 1.0f) * (d - b);
    }
    case BlendMode::Darken:
        return std::min(b, s);
    case BlendMode::Lighten:
        return std::max(b, s);
    case BlendMode::ColorDodge:
        if (b <= 0.0f)
            return 0.0f;
        return s >= 1.0f ? 1.0f : std::min(1.0f, b / (1.0f - s));
    case BlendMode::ColorBurn:
        if (b >= 1.0f)
            return 1.0f;
        return s <= 0.0f ? 0.0f : 1.0f - std::min(1.0f, (1.0f - b) / s);
    case BlendMode::Difference:
        return std::abs(b - s);
    case BlendMode::Normal:
    case BlendMode::Hue:
    case BlendMode::Color:
        break;
    }
    return s;
}

// Reads a tile as floats. False when the layer has nothing there.
bool fetchTile(const TileStore &store, TileCoord c, std::vector<float> &out)
{
    const QImage tile = store.tile(c);
    if (!tile.isNull()) {
        out.resize(size_t(kTilePixels) * 4);
        qFloatFromFloat16(out.data(), reinterpret_cast<const qfloat16 *>(tile.constBits()), kTilePixels * 4);
        return true;
    }
    const Pixel d = store.defaultPixel();
    if (float(d.a) <= 0.0f)
        return false;
    out.resize(size_t(kTilePixels) * 4);
    const float px[4] = {float(d.r), float(d.g), float(d.b), float(d.a)};
    for (int i = 0; i < kTilePixels; ++i)
        std::copy(px, px + 4, out.data() + size_t(i) * 4);
    return true;
}

// Scales a tile's pixels by a mask's values there.
void multiplyByMask(const TileStore &mask, TileCoord c, std::vector<float> &pixels)
{
    const QImage tile = mask.tile(c);
    if (tile.isNull()) {
        const float v = maskValue(mask.defaultPixel());
        if (v < 1.0f)
            for (float &f : pixels)
                f *= v;
        return;
    }
    const auto *m = reinterpret_cast<const Pixel *>(tile.constBits());
    float *p = pixels.data();
    for (int i = 0; i < kTilePixels; ++i, p += 4) {
        const float v = maskValue(m[i]);
        p[0] *= v;
        p[1] *= v;
        p[2] *= v;
        p[3] *= v;
    }
}

QImage toTile(const std::vector<float> &pixels)
{
    QImage tile(TileStore::TileSize, TileStore::TileSize, TileStore::TileFormat);
    qFloatToFloat16(reinterpret_cast<qfloat16 *>(tile.bits()), pixels.data(), kTilePixels * 4);
    return tile;
}

} // namespace

float maskValue(const Pixel &p)
{
    // Luminance of the premultiplied colour: opaque white 1, black or nothing 0.
    return std::clamp(0.2126f * float(p.r) + 0.7152f * float(p.g) + 0.0722f * float(p.b), 0.0f, 1.0f);
}

QString blendModeKey(BlendMode mode)
{
    for (const BlendKey &k : kBlendKeys)
        if (k.mode == mode)
            return QLatin1String(k.key);
    return QStringLiteral("normal");
}

BlendMode blendModeFromKey(const QString &key, bool *ok)
{
    for (const BlendKey &k : kBlendKeys) {
        if (key == QLatin1String(k.key)) {
            if (ok)
                *ok = true;
            return k.mode;
        }
    }
    if (ok)
        *ok = false;
    return BlendMode::Normal;
}

void blendPixels(float *dst, const float *src, int count, float opacity, BlendMode mode)
{
    const Luts &l = luts();
    for (int i = 0; i < count; ++i, dst += 4, src += 4) {
        const float as = src[3] * opacity;
        if (as <= 0.0f)
            continue;
        const float ab = dst[3];
        const float s[3] = {src[0] * opacity, src[1] * opacity, src[2] * opacity};
        if (mode == BlendMode::Normal || ab <= 0.0f) {
            const float k = 1.0f - as;
            dst[0] = s[0] + dst[0] * k;
            dst[1] = s[1] + dst[1] * k;
            dst[2] = s[2] + dst[2] * k;
            dst[3] = as + ab * k;
            continue;
        }

        // Straight colours, as sRGB values.
        float es[3], eb[3], mixed[3];
        const float invAs = 1.0f / as, invAb = 1.0f / ab;
        for (int k = 0; k < 3; ++k) {
            es[k] = encode(l, s[k] * invAs);
            eb[k] = encode(l, dst[k] * invAb);
        }
        if (mode == BlendMode::Hue) {
            std::copy(es, es + 3, mixed);
            setSat(mixed, sat(eb));
            setLum(mixed, lum(eb));
        } else if (mode == BlendMode::Color) {
            std::copy(es, es + 3, mixed);
            setLum(mixed, lum(eb));
        } else {
            for (int k = 0; k < 3; ++k)
                mixed[k] = blendChannel(mode, eb[k], es[k]);
        }
        const float both = as * ab;
        for (int k = 0; k < 3; ++k)
            dst[k] = s[k] * (1.0f - ab) + dst[k] * (1.0f - as) + both * decode(l, mixed[k]);
        dst[3] = as + ab - both;
    }
}

LayerStack::LayerStack(const LayerStack &other)
    : m_layers(other.m_layers)
    , m_size(other.m_size)
    , m_active(other.m_active)
    , m_nextId(other.m_nextId)
    , m_composite(other.m_composite)
{
    m_layers.detach();
}

LayerStack &LayerStack::operator=(const LayerStack &other)
{
    if (this != &other) {
        m_layers = other.m_layers;
        m_layers.detach();
        m_size = other.m_size;
        m_active = other.m_active;
        m_nextId = other.m_nextId;
        m_composite = other.m_composite;
    }
    return *this;
}

LayerStack LayerStack::single(TileStore store, const QSize &size, const QString &name)
{
    LayerStack stack;
    stack.m_size = size;
    Layer l;
    l.name = name;
    l.store = std::move(store);
    stack.insert(std::move(l), 0, 0);
    stack.recompositeAll();
    stack.m_composite.takeDirty();
    return stack;
}

int LayerStack::indexOf(int id) const
{
    for (int i = 0; i < m_layers.size(); ++i)
        if (m_layers.at(i).id == id)
            return i;
    return -1;
}

Layer *LayerStack::layer(int id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_layers[i];
}

const Layer *LayerStack::layer(int id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_layers.at(i);
}

void LayerStack::setActive(int id)
{
    if (indexOf(id) >= 0)
        m_active = id;
}

QList<int> LayerStack::children(int parent) const
{
    QList<int> out;
    for (const Layer &l : m_layers)
        if (l.parent == parent)
            out.append(l.id);
    return out;
}

QList<int> LayerStack::subtree(int id) const
{
    QList<int> out{id};
    for (int i = 0; i < out.size(); ++i)
        out.append(children(out.at(i)));
    return out;
}

bool LayerStack::isInside(int id, int group) const
{
    int hops = 0;
    for (const Layer *l = layer(id); l && l->parent != 0 && hops <= m_layers.size(); l = layer(l->parent), ++hops)
        if (l->parent == group)
            return true;
    return false;
}

bool LayerStack::isShown(int id) const
{
    for (const Layer *l = layer(id); l; l = layer(l->parent)) {
        if (!l->visible)
            return false;
        if (l->parent == 0)
            break;
    }
    return true;
}

bool LayerStack::isLocked(int id) const
{
    for (const Layer *l = layer(id); l; l = layer(l->parent)) {
        if (l->locked)
            return true;
        if (l->parent == 0)
            break;
    }
    return false;
}

QString LayerStack::uniqueName(const QString &base) const
{
    const auto taken = [this](const QString &name) {
        return std::any_of(m_layers.cbegin(), m_layers.cend(), [&](const Layer &l) { return l.name == name; });
    };
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!taken(name))
            return name;
    }
}

int LayerStack::insert(Layer layer, int parent, int position)
{
    if (layer.id == 0)
        layer.id = m_nextId++;
    else
        m_nextId = std::max(m_nextId, layer.id + 1);
    layer.parent = parent;
    const int id = layer.id;

    // Only the order of siblings matters, so place it next to one of them.
    const QList<int> siblings = children(parent);
    int at = int(m_layers.size());
    if (!siblings.isEmpty()) {
        at = position >= siblings.size() ? indexOf(siblings.last()) + 1
                                         : indexOf(siblings.at(std::max(position, 0)));
    }
    m_layers.insert(at, std::move(layer));
    if (m_active == 0)
        m_active = id;
    return id;
}

void LayerStack::remove(int id)
{
    const QList<int> gone = subtree(id);
    const Layer *l = layer(id);
    if (!l)
        return;
    // The active layer moves to a neighbour: the one below, else the one
    // above, else the group it was in.
    if (gone.contains(m_active)) {
        const QList<int> siblings = children(l->parent);
        const int at = int(siblings.indexOf(id));
        m_active = at > 0 ? siblings.at(at - 1) : siblings.size() > 1 ? siblings.at(1) : l->parent;
    }
    m_layers.removeIf([&](const Layer &x) { return gone.contains(x.id); });
    if (m_active == 0 && !m_layers.isEmpty())
        m_active = m_layers.last().id;
}

bool LayerStack::move(int id, int parent, int position)
{
    const int from = indexOf(id);
    if (from < 0 || parent == id || isInside(parent, id))
        return false;
    if (parent != 0) {
        const Layer *p = layer(parent);
        if (!p || !p->group)
            return false;
    }
    Layer l = m_layers.takeAt(from);
    const int active = m_active;
    insert(std::move(l), parent, position);
    m_active = active;
    return true;
}

int LayerStack::duplicate(int id)
{
    const Layer *src = layer(id);
    if (!src)
        return 0;
    const QList<int> ids = subtree(id);
    QHash<int, int> renamed;
    for (int old : ids)
        renamed.insert(old, m_nextId++);

    Layer top = *src;
    top.id = renamed.value(id);
    top.name = QStringLiteral("%1 copy").arg(src->name);
    const int parent = src->parent;
    const int position = int(children(parent).indexOf(id)) + 1;

    // Contents keep their order: copy them in list order.
    QList<Layer> inner;
    for (const Layer &l : m_layers) {
        if (l.id != id && ids.contains(l.id)) {
            Layer c = l;
            c.id = renamed.value(l.id);
            c.parent = renamed.value(l.parent);
            inner.append(std::move(c));
        }
    }
    const int copy = insert(std::move(top), parent, position);
    for (Layer &c : inner)
        m_layers.append(std::move(c));
    return copy;
}

bool LayerStack::mergeDown(int id)
{
    const Layer *top = layer(id);
    if (!top || top->group)
        return false;
    const QList<int> siblings = children(top->parent);
    const int at = int(siblings.indexOf(id));
    if (at <= 0)
        return false;
    Layer *below = layer(siblings.at(at - 1));
    if (!below || below->group)
        return false;

    // The result keeps no mask: what each layer's mask hides is erased first.
    if (below->hasMask)
        applyMask(below->id);
    if (top->hasMask)
        applyMask(id);
    top = layer(id);
    below = layer(siblings.at(at - 1));

    const Layer src = *top; // the list may move under us
    QSet<TileCoord> coords;
    for (const TileCoord c : src.store.tileCoords())
        coords.insert(c);
    const Pixel sd = src.store.defaultPixel();
    if (float(sd.a) > 0.0f) {
        for (const TileCoord c : below->store.tileCoords())
            coords.insert(c);
    }

    std::vector<float> dst, from;
    for (const TileCoord c : coords) {
        if (!fetchTile(src.store, c, from))
            continue;
        if (!fetchTile(below->store, c, dst))
            dst.assign(size_t(kTilePixels) * 4, 0.0f);
        blendPixels(dst.data(), from.data(), kTilePixels, float(src.opacity), src.blend);
        below->store.setTile(c, toTile(dst));
    }
    // Where neither has a tile, the two defaults merge the same way.
    const Pixel bd = below->store.defaultPixel();
    float d[4] = {float(bd.r), float(bd.g), float(bd.b), float(bd.a)};
    const float s[4] = {float(sd.r), float(sd.g), float(sd.b), float(sd.a)};
    blendPixels(d, s, 1, float(src.opacity), src.blend);
    below->store.setDefaultPixel(makePixel(d[0], d[1], d[2], d[3]));

    const int keep = below->id;
    remove(id);
    m_active = keep;
    return true;
}

bool LayerStack::addMask(int id)
{
    Layer *l = layer(id);
    if (!l || l->hasMask)
        return false;
    l->hasMask = true;
    l->maskEnabled = true;
    l->mask = TileStore(Qt::white);
    return true;
}

bool LayerStack::removeMask(int id)
{
    Layer *l = layer(id);
    if (!l || !l->hasMask)
        return false;
    l->hasMask = false;
    l->maskEnabled = true;
    l->mask = TileStore(Qt::white);
    return true;
}

bool LayerStack::applyMask(int id)
{
    Layer *l = layer(id);
    if (!l || !l->hasMask || l->group)
        return false;
    // Tiles the layer has, and where its default pixel shows, tiles the mask has.
    QSet<TileCoord> coords;
    for (const TileCoord c : l->store.tileCoords())
        coords.insert(c);
    const Pixel d = l->store.defaultPixel();
    if (float(d.a) > 0.0f) {
        for (const TileCoord c : l->mask.tileCoords())
            coords.insert(c);
    }
    std::vector<float> pixels;
    for (const TileCoord c : coords) {
        if (!fetchTile(l->store, c, pixels))
            continue;
        multiplyByMask(l->mask, c, pixels);
        l->store.setTile(c, toTile(pixels));
    }
    const float v = maskValue(l->mask.defaultPixel());
    l->store.setDefaultPixel(makePixel(float(d.r) * v, float(d.g) * v, float(d.b) * v, float(d.a) * v));
    return removeMask(id);
}

bool LayerStack::mergeGroup(int id)
{
    const Layer *g = layer(id);
    if (!g || !g->group)
        return false;
    TileStore merged = flattened(id);
    for (int child : children(id))
        remove(child);
    Layer *l = layer(id);
    l->group = false;
    l->store = std::move(merged);
    m_active = id;
    return true;
}

void LayerStack::flatten(const QString &name)
{
    recompositeAll();
    Layer l;
    l.id = m_nextId++;
    l.name = name;
    l.store = m_composite;
    l.store.takeDirty();
    m_layers = {l};
    m_active = l.id;
}

bool LayerStack::rearrange(const QList<QPair<int, int>> &order)
{
    if (order.size() != m_layers.size())
        return false;
    QList<Layer> next;
    QSet<int> seen;
    for (const auto &[id, parent] : order) {
        const Layer *l = layer(id);
        const Layer *p = parent == 0 ? nullptr : layer(parent);
        if (!l || seen.contains(id) || (parent != 0 && (!p || !p->group)))
            return false;
        seen.insert(id);
        next.append(*l);
        next.last().parent = parent;
    }
    LayerStack probe;
    probe.m_layers = next;
    for (const Layer &l : next)
        if (l.parent == l.id || probe.isInside(l.parent, l.id))
            return false;
    m_layers = std::move(next);
    return true;
}

void LayerStack::replaceLayers(QList<Layer> layers, int active)
{
    m_layers = std::move(layers);
    for (const Layer &l : m_layers)
        m_nextId = std::max(m_nextId, l.id + 1);
    m_active = indexOf(active) >= 0 ? active : m_layers.isEmpty() ? 0 : m_layers.last().id;
}

bool LayerStack::contributes(const Layer &l, TileCoord c) const
{
    if (!l.visible || l.opacity <= 0.0)
        return false;
    // A mask tile changes what shows there even where the layer has no tile
    // of its own (a background that is one flat colour, say).
    if (l.hasMask && l.maskEnabled && l.mask.hasTile(c))
        return true;
    if (!l.group)
        return l.store.hasTile(c);
    for (const Layer &child : m_layers)
        if (child.parent == l.id && contributes(child, c))
            return true;
    return false;
}

bool LayerStack::fetchLayer(const Layer &l, TileCoord c, std::vector<float> &out) const
{
    if (l.group ? !compositeTile(l.id, c, out) : !fetchTile(l.store, c, out))
        return false;
    if (l.hasMask && l.maskEnabled)
        multiplyByMask(l.mask, c, out);
    return true;
}

bool LayerStack::compositeTile(int parent, TileCoord c, std::vector<float> &out) const
{
    bool any = false;
    std::vector<float> src;
    for (const Layer &l : m_layers) {
        if (l.parent != parent || !l.visible || l.opacity <= 0.0)
            continue;
        if (!fetchLayer(l, c, src))
            continue;
        if (!any) {
            out.assign(size_t(kTilePixels) * 4, 0.0f);
            any = true;
        }
        blendPixels(out.data(), src.data(), kTilePixels, float(l.opacity), l.blend);
    }
    return any;
}

bool LayerStack::compositeDefault(int parent, float out[4]) const
{
    bool any = false;
    std::fill(out, out + 4, 0.0f);
    for (const Layer &l : m_layers) {
        if (l.parent != parent || !l.visible || l.opacity <= 0.0)
            continue;
        float src[4];
        if (l.group) {
            if (!compositeDefault(l.id, src))
                continue;
        } else {
            const Pixel d = l.store.defaultPixel();
            if (float(d.a) <= 0.0f)
                continue;
            src[0] = float(d.r);
            src[1] = float(d.g);
            src[2] = float(d.b);
            src[3] = float(d.a);
        }
        if (l.hasMask && l.maskEnabled) {
            const float v = maskValue(l.mask.defaultPixel());
            for (float &f : src)
                f *= v;
        }
        blendPixels(out, src, 1, float(l.opacity), l.blend);
        any = true;
    }
    return any;
}

QImage LayerStack::composedTile(TileCoord c) const
{
    // With no tile anywhere, the composite's default pixel already says it.
    const Layer *only = nullptr;
    int contributors = 0;
    for (const Layer &l : m_layers) {
        if (l.parent == 0 && contributes(l, c)) {
            only = &l;
            ++contributors;
        }
    }
    if (contributors == 0)
        return {};
    // One opaque layer and nothing behind it: share its tile.
    if (contributors == 1 && !only->group && only->opacity >= 1.0 && !(only->hasMask && only->maskEnabled)) {
        bool alone = true;
        for (const Layer &l : m_layers) {
            if (l.parent != 0 || &l == only || !l.visible || l.opacity <= 0.0)
                continue;
            float d[4];
            if (l.group ? compositeDefault(l.id, d) : float(l.store.defaultPixel().a) > 0.0f)
                alone = false;
        }
        if (alone)
            return only->store.tile(c);
    }
    std::vector<float> pixels;
    return compositeTile(0, c, pixels) ? toTile(pixels) : QImage();
}

void LayerStack::recomposite(const QSet<TileCoord> &coords)
{
    const QList<TileCoord> list(coords.cbegin(), coords.cend());
    const qsizetype n = list.size();
    std::vector<QImage> tiles(static_cast<size_t>(n));

    // Tiles are independent, and nothing is written until they're all done,
    // so a big job (a layer shown, hidden or faded) is spread over the cores.
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = n >= 64 ? std::min<unsigned>(cores, 16) : 1;
    std::atomic<qsizetype> next{0};
    const auto work = [&] {
        for (qsizetype i = next.fetch_add(1); i < n; i = next.fetch_add(1))
            tiles[static_cast<size_t>(i)] = composedTile(list.at(i));
    };
    if (workers > 1) {
        luts(); // built before the threads start
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < workers; ++t)
            pool.emplace_back(work);
        work();
        for (std::thread &t : pool)
            t.join();
    } else {
        work();
    }
    for (qsizetype i = 0; i < n; ++i)
        m_composite.setTile(list.at(i), tiles[static_cast<size_t>(i)]);
}

QSet<TileCoord> LayerStack::tileCoordsUnder(int parent) const
{
    QSet<TileCoord> coords;
    for (const Layer &l : m_layers) {
        if (parent != 0 && l.parent != parent && !isInside(l.id, parent))
            continue;
        if (!l.group) {
            for (const TileCoord c : l.store.tileCoords())
                coords.insert(c);
        }
        if (l.hasMask) {
            for (const TileCoord c : l.mask.tileCoords())
                coords.insert(c);
        }
    }
    return coords;
}

void LayerStack::updateComposite()
{
    QSet<TileCoord> dirty;
    for (Layer &l : m_layers) {
        if (!l.group)
            dirty.unite(l.store.takeDirty());
        if (l.hasMask)
            dirty.unite(l.mask.takeDirty());
    }
    if (!dirty.isEmpty())
        recomposite(dirty);
}

void LayerStack::recompositeAll()
{
    for (Layer &l : m_layers) {
        l.store.takeDirty();
        l.mask.takeDirty();
    }
    float d[4];
    compositeDefault(0, d);
    m_composite.setDefaultPixel(makePixel(d[0], d[1], d[2], d[3]));

    QSet<TileCoord> coords = tileCoordsUnder(0);
    for (const TileCoord c : m_composite.tileCoords())
        coords.insert(c);
    recomposite(coords);
}

TileStore LayerStack::flattened(int group) const
{
    TileStore out;
    float d[4];
    compositeDefault(group, d);
    out.setDefaultPixel(makePixel(d[0], d[1], d[2], d[3]));
    std::vector<float> pixels;
    for (const TileCoord c : tileCoordsUnder(group))
        if (compositeTile(group, c, pixels))
            out.setTile(c, toTile(pixels));
    out.takeDirty();
    return out;
}

qint64 LayerStack::memoryBytes() const
{
    qint64 n = 0;
    for (const Layer &l : m_layers)
        n += l.store.memoryBytes() + (l.hasMask ? l.mask.memoryBytes() : 0);
    return n;
}

qsizetype LayerStack::tileCount() const
{
    qsizetype n = 0;
    for (const Layer &l : m_layers)
        n += l.store.tileCount() + (l.hasMask ? l.mask.tileCount() : 0);
    return n;
}

LayerStack LayerStack::snapshot() const
{
    LayerStack copy;
    copy.m_layers = m_layers;
    copy.m_layers.detach();
    copy.m_size = m_size;
    copy.m_active = m_active;
    copy.m_nextId = m_nextId;
    return copy;
}

void LayerStack::swapState(LayerStack &other)
{
    m_layers.swap(other.m_layers);
    std::swap(m_size, other.m_size);
    std::swap(m_active, other.m_active);
    // Ids are never reused, whichever way history moves.
    m_nextId = other.m_nextId = std::max(m_nextId, other.m_nextId);
}

qint64 LayerStack::bytesNotSharedWith(const LayerStack &live) const
{
    qint64 bytes = 0;
    for (const Layer &l : m_layers) {
        const Layer *other = live.layer(l.id);
        if (!l.group) {
            for (const TileCoord c : l.store.tileCoords()) {
                if (!other || other->group || other->store.tile(c).cacheKey() != l.store.tile(c).cacheKey())
                    bytes += TileStore::BytesPerTile;
            }
        }
        if (l.hasMask) {
            for (const TileCoord c : l.mask.tileCoords()) {
                if (!other || !other->hasMask || other->mask.tile(c).cacheKey() != l.mask.tile(c).cacheKey())
                    bytes += TileStore::BytesPerTile;
            }
        }
    }
    return bytes;
}

} // namespace easeletch
