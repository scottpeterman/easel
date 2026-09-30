#pragma once

#include "tilepyramid.h"
#include "tilestore.h"

#include <QSize>
#include <QString>

#include <memory>

namespace easel {

// A document read from disk, ready to hand to the UI: tiles imported and the
// zoom pyramid fully built. Safe to produce on a worker thread, since nothing in
// it is shared until it's handed over.
struct LoadedDocument {
    std::unique_ptr<TileStore> store;
    TilePyramid pyramid; // built over *store
    QSize size;
    QString error;       // set when store is null

    bool ok() const { return store != nullptr; }
};

// Decodes an image file (sRGB assumed) into a transparent-default tile store
// and builds its pyramid. Slow for large images: call from a worker thread.
LoadedDocument loadImageDocument(const QString &path);

} // namespace easel
