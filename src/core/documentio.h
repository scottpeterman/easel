#pragma once

#include "layerstack.h"
#include "tilepyramid.h"
#include "tilestore.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace easeletch {

// Native documents are .easeletch files: a zip holding
//   mimetype                 "application/x-easeletch", stored first
//   manifest.json            canvas size, pixel format, and the layers bottom to top:
//                            id, name, type (raster / group / adjustment), parent (the
//                            group it's in, 0 for none), visible, locked, opacity, blend;
//                            an adjustment layer's settings are in "adjustment"
//   preview.png              flattened 8-bit sRGB image, at most 2048 px on its long side
//   layers/<n>/mask/<cx>_<cy>
//                            the same for layer n's mask, when it has one (the
//                            manifest's "mask": enabled, default)
//   layers/<n>/chunks/<cx>_<cy>
//                            for raster layer n (its place in the manifest's list),
//                            the existing tiles of one 16 x 16-tile (1024 px) square:
//                            "EZCH", u32 version 1, u32 count, then per tile
//                            i32 x, i32 y (tile coordinates) and 32768 bytes of
//                            RGBA16F, linear light, premultiplied; all little-endian
// Tiles are stored exactly, so saving and reopening is lossless, and areas never
// painted take no space. Written to a temporary file and renamed over the old
// one, so a failed save never damages the previous file. Limit: Qt's zip writer
// is zip32, so one file must stay under 4 GB (roughly a fully painted
// 20000 x 20000 canvas).
inline constexpr char NativeSuffix[] = "easeletch";
// The app was first called Easel: its .easel files (manifest format "easel")
// still open. They aren't written back to; saving one asks for a new name.
inline constexpr char LegacySuffix[] = "easel";
// Version 1 held a single layer; version 2 holds the layer stack; version 3
// adds layer masks; version 4 adds adjustment layers. A document with no
// adjustment layers is still written as version 3, so older builds open it.
inline constexpr int FormatVersion = 4;
inline constexpr int FormatVersionWithoutAdjustments = 3;
inline constexpr int PreviewMaxSide = 2048;
inline constexpr int ChunkTiles = 16; // tiles per chunk side

// A document read from disk, ready to hand to the UI: tiles imported, layers
// composited and the zoom pyramid fully built over the composite. Safe to produce on a worker thread, since nothing in
// it is shared until it's handed over.
struct LoadedDocument {
    std::unique_ptr<LayerStack> stack;
    TilePyramid pyramid; // built over stack->composite()
    QSize size;
    bool native = false; // came from an .easeletch file (so Save can write back to it)
    QString error;       // set when stack is null

    bool ok() const { return stack != nullptr; }
};

// An .easeletch file, or an .easel file from before the rename.
bool isNativeDocument(const QString &path);
bool isLegacyDocument(const QString &path);

// Opens an .easeletch document, or decodes an image file (sRGB assumed) into a
// single layer, and builds its pyramid. Slow for large images:
// call from a worker thread.
LoadedDocument loadDocument(const QString &path);
LoadedDocument loadImageDocument(const QString &path);
LoadedDocument loadNativeDocument(const QString &path);

// Writes an .easeletch file. Returns an empty string on success, else the error.
// Takes the stack by value: pass a copy (with its composite up to date) and
// the call can run on a worker thread while painting continues.
QString saveNativeDocument(const QString &path, LayerStack stack);
// A single-layer document from one store.
QString saveNativeDocument(const QString &path, TileStore store, const QSize &size);

// Flattened 8-bit sRGB image of the canvas at 1 / 2^level of full size.
QImage flattenImage(const TileStore &store, const QSize &size, int level = 0);

// Writes a flattened image in the format the suffix names (png, jpg, webp, ...).
// Formats without alpha are composited over white.
QString exportImage(const QString &path, const TileStore &store, const QSize &size, int quality = 92);
// Writes an 8-bit image the same way (e.g. an exported selection).
QString writeImageFile(const QString &path, QImage image, int quality = 92);

} // namespace easeletch
