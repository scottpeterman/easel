#include "documentio.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QThread>
#include <QtEndian>

#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace easeletch {

namespace {

constexpr char kMimeType[] = "application/x-easeletch";
constexpr char kPixelFormat[] = "rgba16f-linear-premultiplied-le";
constexpr qint64 kTileBytes = TileStore::BytesPerTile;

LoadedDocument failed(const QString &error)
{
    LoadedDocument doc;
    doc.error = error;
    return doc;
}

void finish(LoadedDocument &doc, std::unique_ptr<LayerStack> stack)
{
    stack->recompositeAll();
    stack->compositeStore()->takeDirty(); // a fresh document has nothing to redraw incrementally
    doc.size = stack->size();
    doc.pyramid.setBase(stack->compositeStore(), doc.size);
    doc.pyramid.buildAll();
    doc.stack = std::move(stack);
}

QJsonArray pixelToJson(const Pixel &p)
{
    return {double(float(p.r)), double(float(p.g)), double(float(p.b)), double(float(p.a))};
}

Pixel pixelFromJson(const QJsonArray &a)
{
    if (a.size() != 4)
        return makePixel(0, 0, 0, 0);
    return makePixel(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()),
                     float(a.at(3).toDouble()));
}

constexpr char kChunkMagic[4] = {'E', 'Z', 'C', 'H'};
constexpr quint32 kChunkVersion = 1;

// Where a page's tiles go in the zip: nowhere special for a single-page
// document, "pages/<p>/" otherwise.
QString pagePrefix(int page, bool paged)
{
    return paged ? QStringLiteral("pages/%1/").arg(page) : QString();
}

QString chunkEntry(const QString &prefix, int layer, const char *kind, int cx, int cy)
{
    return prefix + QStringLiteral("layers/%1/%2/%3_%4").arg(layer).arg(QLatin1String(kind)).arg(cx).arg(cy);
}

QString defaultPageName(int index)
{
    return QObject::tr("Page %1").arg(index + 1);
}

// A page as the manifest describes it, before its tiles are read.
struct PendingPage {
    QString name;
    QSize size;
    int activeLayer = 0;
    QList<Layer> list;
    QHash<int, int> byEntry; // place in the manifest's layer list -> place in list
};

// Reads a page's size and layers from the manifest object describing it.
// Returns the error, or an empty string.
QString readPage(const QJsonObject &m, PendingPage &page)
{
    page.size = QSize(m.value(QLatin1String("width")).toInt(), m.value(QLatin1String("height")).toInt());
    page.activeLayer = m.value(QLatin1String("activeLayer")).toInt(0);
    const QJsonArray layers = m.value(QLatin1String("layers")).toArray();
    if (page.size.isEmpty() || layers.isEmpty())
        return QObject::tr("The document is damaged (no canvas or layers).");

    // Version 1 files list one layer with no id; it reads as a raster layer.
    QList<Layer> &list = page.list;
    for (int i = 0; i < layers.size(); ++i) {
        const QJsonObject o = layers.at(i).toObject();
        Layer l;
        l.id = o.value(QLatin1String("id")).toInt(i + 1);
        l.name = o.value(QLatin1String("name")).toString();
        const QString type = o.value(QLatin1String("type")).toString();
        l.group = type == QLatin1String("group");
        if (type == QLatin1String("adjustment")) {
            l.adjust = Adjustment::fromJson(o.value(QLatin1String("adjustment")).toObject());
            if (!l.isAdjustment())
                return QObject::tr("The document is damaged (layer %1).").arg(i);
        }
        l.parent = o.value(QLatin1String("parent")).toInt(0);
        l.visible = o.value(QLatin1String("visible")).toBool(true);
        l.locked = o.value(QLatin1String("locked")).toBool(false);
        l.opacity = std::clamp(o.value(QLatin1String("opacity")).toDouble(1.0), 0.0, 1.0);
        l.blend = blendModeFromKey(o.value(QLatin1String("blend")).toString());
        l.store.setDefaultPixel(pixelFromJson(o.value(QLatin1String("default")).toArray()));
        if (const QJsonValue mask = o.value(QLatin1String("mask")); mask.isObject()) {
            const QJsonObject mo = mask.toObject();
            l.hasMask = true;
            l.maskEnabled = mo.value(QLatin1String("enabled")).toBool(true);
            l.mask.setDefaultPixel(pixelFromJson(mo.value(QLatin1String("default")).toArray()));
        }
        if (l.id <= 0)
            return QObject::tr("The document is damaged (layer %1).").arg(i);
        page.byEntry.insert(i, int(list.size()));
        list.append(std::move(l));
    }
    // A parent must be a group in the file, and no group may contain itself.
    for (Layer &l : list) {
        const auto parent = std::find_if(list.cbegin(), list.cend(),
                                         [&](const Layer &p) { return p.id == l.parent && p.group; });
        if (l.parent != 0 && (parent == list.cend() || l.parent == l.id))
            l.parent = 0;
    }
    for (Layer &l : list) {
        int hops = 0;
        for (int p = l.parent; p != 0 && hops <= list.size(); ++hops) {
            const auto it = std::find_if(list.cbegin(), list.cend(), [&](const Layer &x) { return x.id == p; });
            p = it == list.cend() ? 0 : it->parent;
        }
        if (hops > list.size())
            l.parent = 0;
    }
    return {};
}

// A page's layers as the manifest lists them.
QJsonArray layersToJson(const LayerStack &stack, const QString &prefix)
{
    QJsonArray layerList;
    for (int i = 0; i < stack.count(); ++i) {
        const Layer &l = stack.layers().at(i);
        QJsonObject o{
            {QLatin1String("id"), l.id},
            {QLatin1String("name"), l.name},
            {QLatin1String("type"),
             QLatin1String(l.group ? "group" : l.isAdjustment() ? "adjustment" : "raster")},
            {QLatin1String("parent"), l.parent},
            {QLatin1String("visible"), l.visible},
            {QLatin1String("locked"), l.locked},
            {QLatin1String("opacity"), l.opacity},
            {QLatin1String("blend"), blendModeKey(l.blend)},
        };
        if (l.isAdjustment())
            o.insert(QLatin1String("adjustment"), l.adjust.toJson());
        if (l.hasPixels()) {
            o.insert(QLatin1String("default"), pixelToJson(l.store.defaultPixel()));
            o.insert(QLatin1String("chunks"), prefix + QStringLiteral("layers/%1/chunks/").arg(i));
        }
        if (l.hasMask) {
            o.insert(QLatin1String("mask"),
                     QJsonObject{{QLatin1String("enabled"), l.maskEnabled},
                                 {QLatin1String("default"), pixelToJson(l.mask.defaultPixel())},
                                 {QLatin1String("chunks"), prefix + QStringLiteral("layers/%1/mask/").arg(i)}});
        }
        layerList.append(o);
    }
    return layerList;
}

int floorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

template<typename T>
void putLE(QByteArray &out, T v)
{
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char *>(&v), sizeof(T));
}

template<typename T>
T getLE(const char *p)
{
    T v;
    std::memcpy(&v, p, sizeof(T));
    return qFromLittleEndian(v);
}

void appendTileData(QByteArray &out, const QImage &tile)
{
    const qsizetype at = out.size();
    out.append(reinterpret_cast<const char *>(tile.constBits()), qsizetype(kTileBytes));
    if constexpr (Q_BYTE_ORDER != Q_LITTLE_ENDIAN) {
        auto *p = reinterpret_cast<quint16 *>(out.data() + at);
        for (qint64 i = 0; i < kTileBytes / 2; ++i)
            p[i] = qToLittleEndian(p[i]);
    }
}

QImage tileFromData(const char *data)
{
    QImage tile(TileStore::TileSize, TileStore::TileSize, TileStore::TileFormat);
    if constexpr (Q_BYTE_ORDER == Q_LITTLE_ENDIAN) {
        std::memcpy(tile.bits(), data, size_t(kTileBytes));
    } else {
        auto *dst = reinterpret_cast<quint16 *>(tile.bits());
        for (qint64 i = 0; i < kTileBytes / 2; ++i)
            dst[i] = getLE<quint16>(data + 2 * i);
    }
    return tile;
}

// The .part file a save writes before renaming it over the target. Plain QFile,
// not QTemporaryFile: on Windows QTemporaryFile::close() keeps the OS handle
// open (it only rewinds, so the file can be reopened), and an open handle
// blocks the rename until the object is destroyed.
struct PartFile
{
    QFile file;
    bool created = false;
    bool keep = false;

    ~PartFile()
    {
        if (created && !keep)
            file.remove(); // closes first
    }

    // Creates ".<name>.<random>.part" next to target; empty on success.
    QString create(const QFileInfo &target)
    {
        const QDir dir = target.absoluteDir();
        for (int attempt = 0;; ++attempt) {
            const QString suffix = QString::number(QRandomGenerator::global()->generate(), 16);
            file.setFileName(dir.filePath(QStringLiteral(".%1.%2.part").arg(target.fileName(), suffix)));
            if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                created = true;
                return {};
            }
            // A name collision is retried; anything else (no such folder, no
            // permission) fails the same way every time.
            if (!file.exists() || attempt >= 10)
                return file.errorString();
        }
    }
};

// Windows: a virus scanner or the search indexer opens a file the moment it is
// closed, without sharing delete access, so a rename right after writing can
// fail for a moment. These are the errors that clear up on their own.
bool isTransientRenameError(const std::error_code &ec)
{
#ifdef Q_OS_WIN
    if (ec.category() == std::system_category()) {
        switch (ec.value()) {
        case 5:  // ERROR_ACCESS_DENIED
        case 32: // ERROR_SHARING_VIOLATION
        case 33: // ERROR_LOCK_VIOLATION
            return true;
        default:
            break;
        }
    }
#else
    Q_UNUSED(ec)
#endif
    return false;
}

} // namespace

bool isNativeDocument(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String(NativeSuffix), Qt::CaseInsensitive) == 0
           || isLegacyDocument(path);
}

bool isLegacyDocument(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String(LegacySuffix), Qt::CaseInsensitive) == 0;
}

LoadedDocument loadDocument(const QString &path)
{
    return isNativeDocument(path) ? loadNativeDocument(path) : loadImageDocument(path);
}

LoadedDocument loadImageDocument(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return failed(reader.errorString());

    TileStore store(QColor(0, 0, 0, 0));
    store.writeImage(image);
    const QSize size = image.size();
    image = QImage(); // free the decoded copy before building the pyramid

    LoadedDocument doc;
    finish(doc, std::make_unique<LayerStack>(LayerStack::single(std::move(store), size, QObject::tr("Background"))));
    LoadedPage page;
    page.name = defaultPageName(0);
    doc.pages.push_back(std::move(page));
    return doc;
}

LoadedDocument loadNativeDocument(const QString &path)
{
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return failed(QObject::tr("The file can't be read."));

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(zip.fileData(QStringLiteral("manifest.json")), &parseError);
    const QJsonObject m = json.object();
    const QString format = m.value(QLatin1String("format")).toString();
    if (json.isNull() || (format != QLatin1String("easeletch") && format != QLatin1String(LegacySuffix)))
        return failed(QObject::tr("This isn't an Easeletch document."));
    const int version = m.value(QLatin1String("version")).toInt();
    if (version < 1 || version > FormatVersion)
        return failed(QObject::tr("This document was saved by a newer Easeletch (format %1).").arg(version));
    if (m.value(QLatin1String("pixelFormat")).toString() != QLatin1String(kPixelFormat)
        || m.value(QLatin1String("tileSize")).toInt() != TileStore::TileSize)
        return failed(QObject::tr("Unsupported pixel format."));

    // Format 5 lists pages; before that the document is the one page.
    QList<PendingPage> pages;
    const bool paged = m.contains(QLatin1String("pages"));
    if (paged) {
        const QJsonArray list = m.value(QLatin1String("pages")).toArray();
        if (list.isEmpty())
            return failed(QObject::tr("The document is damaged (no pages)."));
        for (int i = 0; i < list.size(); ++i) {
            const QJsonObject o = list.at(i).toObject();
            PendingPage page;
            page.name = o.value(QLatin1String("name")).toString();
            if (const QString error = readPage(o, page); !error.isEmpty())
                return failed(error);
            pages.append(std::move(page));
        }
    } else {
        PendingPage page;
        page.name = m.value(QLatin1String("pageName")).toString();
        if (const QString error = readPage(m, page); !error.isEmpty())
            return failed(error);
        pages.append(std::move(page));
    }
    for (int i = 0; i < pages.size(); ++i)
        if (pages[i].name.trimmed().isEmpty())
            pages[i].name = defaultPageName(i);

    // Qt's zip reader looks entries up by name with a linear scan, so walk the
    // entry list once rather than asking for each chunk by name.
    static const QRegularExpression chunkName(
        QStringLiteral("^(?:pages/(\\d+)/)?layers/(\\d+)/(chunks|mask)/(-?\\d+)_(-?\\d+)$"));
    for (const QZipReader::FileInfo &info : zip.fileInfoList()) {
        const QRegularExpressionMatch match = chunkName.match(info.filePath);
        if (!match.hasMatch())
            continue;
        if (match.captured(1).isEmpty() == paged)
            continue; // not where this kind of document keeps its tiles
        const int pageIndex = paged ? match.captured(1).toInt() : 0;
        if (pageIndex < 0 || pageIndex >= pages.size())
            continue;
        PendingPage &page = pages[pageIndex];
        const auto entry = page.byEntry.constFind(match.captured(2).toInt());
        if (entry == page.byEntry.cend())
            continue;
        Layer &layer = page.list[entry.value()];
        const bool isMask = match.captured(3) == QLatin1String("mask");
        if (isMask ? !layer.hasMask : !layer.hasPixels())
            continue;
        TileStore &store = isMask ? layer.mask : layer.store;
        const QByteArray data = zip.fileData(info.filePath);
        const char *p = data.constData();
        constexpr qsizetype header = 12, record = 8 + kTileBytes;
        if (data.size() < header || std::memcmp(p, kChunkMagic, 4) != 0
            || getLE<quint32>(p + 4) != kChunkVersion)
            return failed(QObject::tr("The document is damaged (%1).").arg(info.filePath));
        const quint32 count = getLE<quint32>(p + 8);
        if (data.size() != header + qsizetype(count) * record)
            return failed(QObject::tr("The document is damaged (%1).").arg(info.filePath));
        for (quint32 i = 0; i < count; ++i) {
            const char *r = p + header + qsizetype(i) * record;
            store.setTile({getLE<qint32>(r), getLE<qint32>(r + 4)}, tileFromData(r + 8));
        }
    }
    if (zip.status() != QZipReader::NoError)
        return failed(QObject::tr("The file can't be read completely."));

    LoadedDocument doc;
    doc.native = !isLegacyDocument(path);
    doc.activePage = std::clamp(m.value(QLatin1String("activePage")).toInt(0), 0, int(pages.size()) - 1);
    for (int i = 0; i < pages.size(); ++i) {
        PendingPage &page = pages[i];
        auto stack = std::make_unique<LayerStack>();
        stack->setSize(page.size);
        stack->replaceLayers(std::move(page.list), page.activeLayer);
        LoadedPage loaded;
        loaded.name = page.name;
        if (i == doc.activePage) {
            finish(doc, std::move(stack)); // composited, with the pyramid built
        } else {
            stack->recompositeAll();
            stack->compositeStore()->takeDirty();
            loaded.stack = std::move(stack);
        }
        doc.pages.push_back(std::move(loaded));
    }
    return doc;
}

QString saveNativeDocument(const QString &path, TileStore store, const QSize &size)
{
    return saveNativeDocument(path, LayerStack::single(std::move(store), size, QObject::tr("Background")));
}

QString saveNativeDocument(const QString &path, LayerStack stack)
{
    QList<DocumentPage> pages;
    pages.append({defaultPageName(0), std::move(stack)});
    return saveNativeDocument(path, std::move(pages), 0);
}

QString saveNativeDocument(const QString &path, QList<DocumentPage> pages, int activePage)
{
    if (pages.isEmpty())
        return QObject::tr("The document has no pages.");
    activePage = std::clamp(activePage, 0, int(pages.size()) - 1);
    const bool paged = pages.size() > 1;
    // Write next to the target, then rename over it: the old file is replaced
    // in one step, or not at all. (QSaveFile would do this, but QZipWriter
    // closes its device when done, which QSaveFile doesn't allow.)
    const QFileInfo target(path);
    PartFile part;
    if (const QString error = part.create(target); !error.isEmpty())
        return error;
    QFile &file = part.file;

    {
        QZipWriter zip(&file);
        // Tile data is noisy half-floats; deflate helps flat areas and costs time
        // on the rest. AutoCompress keeps whichever is smaller.
        zip.setCompressionPolicy(QZipWriter::NeverCompress);
        zip.addFile(QStringLiteral("mimetype"), QByteArray(kMimeType));
        zip.setCompressionPolicy(QZipWriter::AutoCompress);

        // The oldest version that can hold the document.
        bool anyAdjustment = false, anyThreshold = false;
        for (const DocumentPage &page : std::as_const(pages))
            for (const Layer &l : page.stack.layers()) {
                anyAdjustment = anyAdjustment || l.isAdjustment();
                anyThreshold = anyThreshold || l.adjust.type == AdjustmentType::Threshold;
            }
        const int version = anyThreshold ? FormatVersion
                            : paged ? FormatVersionWithoutThreshold
                            : anyAdjustment ? FormatVersionWithoutPages
                                            : FormatVersionWithoutAdjustments;
        QJsonObject manifest{
            {QLatin1String("format"), QLatin1String("easeletch")},
            {QLatin1String("version"), version},
            {QLatin1String("tileSize"), TileStore::TileSize},
            {QLatin1String("chunkTiles"), ChunkTiles},
            {QLatin1String("pixelFormat"), QLatin1String(kPixelFormat)},
        };
        const auto describe = [&](QJsonObject &o, int index) {
            const LayerStack &stack = pages.at(index).stack;
            o.insert(QLatin1String("width"), stack.size().width());
            o.insert(QLatin1String("height"), stack.size().height());
            o.insert(QLatin1String("activeLayer"), stack.activeId());
            o.insert(QLatin1String("layers"), layersToJson(stack, pagePrefix(index, paged)));
        };
        if (paged) {
            QJsonArray pageList;
            for (int i = 0; i < pages.size(); ++i) {
                QJsonObject o{{QLatin1String("name"), pages.at(i).name}};
                describe(o, i);
                pageList.append(o);
            }
            manifest.insert(QLatin1String("pages"), pageList);
            manifest.insert(QLatin1String("activePage"), activePage);
        } else {
            manifest.insert(QLatin1String("pageName"), pages.at(0).name);
            describe(manifest, 0);
        }
        zip.addFile(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson());

        // Preview (of the active page): from the coarsest pyramid level still
        // at least 2048 px across (cheap, and already filtered in linear
        // light), scaled to fit.
        const QSize size = pages.at(activePage).stack.size();
        const int side = std::max(size.width(), size.height());
        int level = 0;
        while ((side >> (level + 1)) >= PreviewMaxSide)
            ++level;
        QImage preview = flattenImage(pages.at(activePage).stack.composite(), size, level);
        if (std::max(preview.width(), preview.height()) > PreviewMaxSide)
            preview = preview.scaled(PreviewMaxSide, PreviewMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        preview.save(&buffer, "PNG");
        zip.addFile(QStringLiteral("preview.png"), png);

        // Group tiles into chunks: few zip entries (fast to open), each small.
        const auto writeStore = [&](const QString &prefix, const QRect &canvas, int index, const char *kind,
                                    const TileStore &tiles) {
            QHash<TileCoord, QList<TileCoord>> chunks;
            for (const TileCoord c : tiles.tileCoords()) {
                if (!TileStore::tileRect(c).intersects(canvas))
                    continue; // nothing outside the canvas is ever shown
                chunks[{floorDiv(c.x, ChunkTiles), floorDiv(c.y, ChunkTiles)}].append(c);
            }
            for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
                QByteArray data;
                data.reserve(12 + it.value().size() * (8 + kTileBytes));
                data.append(kChunkMagic, 4);
                putLE<quint32>(data, kChunkVersion);
                putLE<quint32>(data, quint32(it.value().size()));
                for (const TileCoord c : it.value()) {
                    putLE<qint32>(data, c.x);
                    putLE<qint32>(data, c.y);
                    appendTileData(data, tiles.tile(c));
                }
                zip.addFile(chunkEntry(prefix, index, kind, it.key().x, it.key().y), data);
            }
        };
        for (int p = 0; p < pages.size(); ++p) {
            const LayerStack &stack = pages.at(p).stack;
            const QString prefix = pagePrefix(p, paged);
            const QRect canvas(QPoint(0, 0), stack.size());
            for (int i = 0; i < stack.count(); ++i) {
                const Layer &l = stack.layers().at(i);
                if (l.hasPixels())
                    writeStore(prefix, canvas, i, "chunks", l.store);
                if (l.hasMask)
                    writeStore(prefix, canvas, i, "mask", l.mask);
            }
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError)
            return QObject::tr("Writing the document failed.");
    }

    // QZipWriter closes the device; make sure of it, since an open handle can't
    // be renamed on Windows.
    file.close();

    // A new file gets the usual permissions; a replaced one keeps its own.
    if (target.exists())
        QFile::setPermissions(file.fileName(), QFile(path).permissions());

    const std::filesystem::path from(file.fileName().toStdU16String());
    const std::filesystem::path to(path.toStdU16String());
    std::error_code ec;
    // Up to about two seconds of retries, as git for Windows and MSBuild do.
    for (int attempt = 0;; ++attempt) {
        std::filesystem::rename(from, to, ec);
        if (!ec || attempt >= 100 || !isTransientRenameError(ec))
            break;
        QThread::msleep(20);
    }
    if (ec)
        return QString::fromStdString(ec.message());
    part.keep = true; // it's the saved document now
    return {};
}

QImage flattenImage(const TileStore &store, const QSize &size, int level)
{
    TilePyramid pyramid;
    pyramid.setBase(&store, size);
    level = std::clamp(level, 0, pyramid.topLevel());

    const int scale = 1 << level;
    const QSize out((size.width() + scale - 1) / scale, (size.height() + scale - 1) / scale);
    QImage image(out, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor::fromRgba(pixelToDisplay(store.defaultPixel())));

    QPainter p(&image);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    const QRect canvas(QPoint(0, 0), size);
    for (const TileCoord c : TilePyramid::tilesIntersecting(level, canvas)) {
        const QImage tile = pyramid.tile({level, c});
        if (tile.isNull())
            continue; // reads as the default, already filled
        p.drawImage(QPoint(c.x * TileStore::TileSize, c.y * TileStore::TileSize), TileStore::toDisplay(tile));
    }
    p.end();
    return image;
}

QString exportImage(const QString &path, const TileStore &store, const QSize &size, int quality)
{
    return writeImageFile(path, flattenImage(store, size, 0), quality);
}

QString writeImageFile(const QString &path, QImage image, int quality)
{
    const QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
    const bool hasAlpha = format == "png" || format == "webp" || format == "tif" || format == "tiff";
    if (!hasAlpha) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, image);
        p.end();
        image = flat;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    QImageWriter writer(&file, format);
    writer.setQuality(quality);
    if (!writer.write(image))
        return writer.errorString();
    if (!file.commit())
        return file.errorString();
    return {};
}

} // namespace easeletch
