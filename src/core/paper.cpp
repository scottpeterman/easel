#include "paper.h"

#include "brush.h"

#include <algorithm>
#include <cmath>

namespace easeletch {

const QList<PaperStyle> &paperStyles()
{
    const auto make = [](const char *id, const QString &name, const char *hex, double tooth, double toothSize,
                         double fibres, double mottle) {
        PaperStyle p;
        p.id = QString::fromLatin1(id);
        p.name = name;
        p.base = QColor(QString::fromLatin1(hex));
        p.tooth = tooth;
        p.toothSize = toothSize;
        p.fibres = fibres;
        p.mottle = mottle;
        return p;
    };
    static const QList<PaperStyle> papers = {
        // Off-white rag paper: a fine tooth and a few fibres.
        make("cotton", QStringLiteral("Cotton"), "#f6f3ea", 0.035, 2.2, 0.02, 0.0),
        // Watercolour paper: the same white, a much rougher surface.
        make("coldpress", QStringLiteral("Cold press"), "#f3f0e6", 0.06, 3.6, 0.0, 0.01),
        make("cream", QStringLiteral("Cream"), "#f2e8d0", 0.03, 1.8, 0.0, 0.0),
        // Skin, not pulp: smooth, and never one even colour.
        make("parchment", QStringLiteral("Parchment"), "#e4d0a4", 0.025, 2.0, 0.0, 0.04),
        make("sepia", QStringLiteral("Sepia"), "#c9b08c", 0.04, 2.2, 0.01, 0.025),
        make("kraft", QStringLiteral("Kraft"), "#b5946b", 0.045, 2.4, 0.04, 0.015),
        make("grey", QStringLiteral("Grey"), "#b8b8b2", 0.04, 2.2, 0.0, 0.0),
        // For chalk and light pencil.
        make("black", QStringLiteral("Black"), "#1c1c1e", 0.03, 2.4, 0.01, 0.0),
    };
    return papers;
}

const PaperStyle *paperStyle(const QString &id)
{
    for (const PaperStyle &p : paperStyles())
        if (p.id == id)
            return &p;
    return nullptr;
}

namespace {

// How much lighter (+) or darker (-) than its plain colour the paper is at a
// pixel, as a fraction of full brightness.
float surfaceAt(const PaperStyle &style, int x, int y)
{
    float v = 0.0f;
    if (style.tooth > 0.0)
        v += float(style.tooth) * (paperTooth(x, y, style.toothSize) - 0.5f) * 2.0f;
    if (style.fibres > 0.0) {
        // Long thin streaks, lying both ways.
        const float across = smoothNoise(x, y, 18.0, 1.6, 11) - 0.5f;
        const float down = smoothNoise(x, y, 1.6, 18.0, 12) - 0.5f;
        // Only the strongest show, as separate fibres.
        const float a = std::max(0.0f, std::abs(across) - 0.28f), d = std::max(0.0f, std::abs(down) - 0.28f);
        v += float(style.fibres) * 4.5f * (std::copysign(a, across) + std::copysign(d, down));
    }
    if (style.mottle > 0.0)
        // Broad and slow: seen across the whole sheet it should read as an
        // uneven tone, not as separate blotches.
        v += float(style.mottle) * ((smoothNoise(x, y, 420.0, 420.0, 21) - 0.5f) * 1.4f
                                    + (smoothNoise(x, y, 130.0, 130.0, 22) - 0.5f) * 0.6f);
    return v;
}

void channelsAt(const PaperStyle &style, int x, int y, float rgb[3])
{
    const float v = surfaceAt(style, x, y);
    rgb[0] = std::clamp(float(style.base.redF()) + v, 0.0f, 1.0f);
    rgb[1] = std::clamp(float(style.base.greenF()) + v, 0.0f, 1.0f);
    rgb[2] = std::clamp(float(style.base.blueF()) + v, 0.0f, 1.0f);
}

} // namespace

QColor paperColorAt(const PaperStyle &style, int x, int y)
{
    float rgb[3];
    channelsAt(style, x, y, rgb);
    return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
}

void fillPaper(TileStore &store, const QRect &canvas, const PaperStyle &style)
{
    constexpr int N = TileStore::TileSize;
    store.clear();
    store.setDefaultPixel(makePixel(srgbToLinear(float(style.base.redF())), srgbToLinear(float(style.base.greenF())),
                                    srgbToLinear(float(style.base.blueF())), 1.0f));
    if (style.tooth <= 0.0 && style.fibres <= 0.0 && style.mottle <= 0.0)
        return; // a plain sheet is its colour, and takes no memory
    for (const TileCoord c : TileStore::tilesIntersecting(canvas)) {
        const QRect tr = TileStore::tileRect(c);
        const QRect part = tr & canvas;
        auto *out = reinterpret_cast<Pixel *>(store.writableTile(c).bits());
        for (int y = part.top(); y <= part.bottom(); ++y) {
            Pixel *row = out + (y - tr.top()) * N;
            for (int x = part.left(); x <= part.right(); ++x) {
                float rgb[3];
                channelsAt(style, x, y, rgb);
                row[x - tr.left()] = makePixel(srgbToLinear(rgb[0]), srgbToLinear(rgb[1]), srgbToLinear(rgb[2]), 1.0f);
            }
        }
    }
}

QImage paperPreview(const PaperStyle &style, const QSize &size)
{
    QImage out(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
            line[x] = paperColorAt(style, x, y).rgb();
    }
    return out;
}

} // namespace easeletch
