#include "documentio.h"

#include <QImageReader>

namespace easel {

LoadedDocument loadImageDocument(const QString &path)
{
    LoadedDocument doc;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        doc.error = reader.errorString();
        return doc;
    }

    auto store = std::make_unique<TileStore>(QColor(0, 0, 0, 0));
    store->writeImage(image);
    store->takeDirty(); // a fresh document has nothing to redraw incrementally
    doc.size = image.size();
    image = QImage();   // free the decoded copy before building the pyramid

    doc.pyramid.setBase(store.get(), doc.size);
    doc.pyramid.buildAll();
    doc.store = std::move(store);
    return doc;
}

} // namespace easel
