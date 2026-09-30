#pragma once

#include "tilepyramid.h"
#include "tilestore.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace easel {

// Native documents are .easel files: a zip holding
//   mimetype                 "application/x-easel", stored first
//   manifest.json            canvas size, pixel format, layer list
//   preview.png              flattened 8-bit sRGB image, at most 2048 px on its long side
//   layers/<n>/chunks/<cx>_<cy>
//                            the existing tiles of one 16 x 16-tile (1024 px) square:
//                            "EZCH", u32 version 1, u32 count, then per tile
//                            i32 x, i32 y (tile coordinates) and 32768 bytes of
//                            RGBA16F, linear light, premultiplied; all little-endian
// Tiles are stored exactly, so saving and reopening is lossless, and areas never
// painted take no space. Written to a temporary file and renamed over the old
// one, so a failed save never damages the previous file. Limit: Qt's zip writer
// is zip32, so one file must stay under 4 GB (roughly a fully painted
// 20000 x 20000 canvas).
inline constexpr char NativeSuffix[] = "easel";
inline constexpr int FormatVersion = 1;
inline constexpr int PreviewMaxSide = 2048;
inline constexpr int ChunkTiles = 16; // tiles per chunk side

// A document read from disk, ready to hand to the UI: tiles imported and the
// zoom pyramid fully built. Safe to produce on a worker thread, since nothing in
// it is shared until it's handed over.
struct LoadedDocument {
    std::unique_ptr<TileStore> store;
    TilePyramid pyramid; // built over *store
    QSize size;
    bool native = false; // came from an .easel file (so Save can write back to it)
    QString error;       // set when store is null

    bool ok() const { return store != nullptr; }
};

bool isNativeDocument(const QString &path);

// Opens an .easel document or decodes an image file (sRGB assumed) into a
// transparent-default store, and builds its pyramid. Slow for large images:
// call from a worker thread.
LoadedDocument loadDocument(const QString &path);
LoadedDocument loadImageDocument(const QString &path);
LoadedDocument loadNativeDocument(const QString &path);

// Writes an .easel file. Returns an empty string on success, else the error.
// Takes the store by value: pass a snapshot and the call can run on a worker
// thread while painting continues.
QString saveNativeDocument(const QString &path, TileStore store, const QSize &size);

// Flattened 8-bit sRGB image of the canvas at 1 / 2^level of full size.
QImage flattenImage(const TileStore &store, const QSize &size, int level = 0);

// Writes a flattened image in the format the suffix names (png, jpg, webp, ...).
// Formats without alpha are composited over white.
QString exportImage(const QString &path, const TileStore &store, const QSize &size, int quality = 92);
// Writes an 8-bit image the same way (e.g. an exported selection).
QString writeImageFile(const QString &path, QImage image, int quality = 92);

} // namespace easel
