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
#include <QTemporaryFile>
#include <QtEndian>

#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace easel {

namespace {

constexpr char kMimeType[] = "application/x-easel";
constexpr char kPixelFormat[] = "rgba16f-linear-premultiplied-le";
constexpr qint64 kTileBytes = TileStore::BytesPerTile;

LoadedDocument failed(const QString &error)
{
    LoadedDocument doc;
    doc.error = error;
    return doc;
}

void finish(LoadedDocument &doc, std::unique_ptr<TileStore> store, const QSize &size)
{
    store->takeDirty(); // a fresh document has nothing to redraw incrementally
    doc.size = size;
    doc.pyramid.setBase(store.get(), size);
    doc.pyramid.buildAll();
    doc.store = std::move(store);
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

QString chunkEntry(int layer, int cx, int cy)
{
    return QStringLiteral("layers/%1/chunks/%2_%3").arg(layer).arg(cx).arg(cy);
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

} // namespace

bool isNativeDocument(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String(NativeSuffix), Qt::CaseInsensitive) == 0;
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

    auto store = std::make_unique<TileStore>(QColor(0, 0, 0, 0));
    store->writeImage(image);
    const QSize size = image.size();
    image = QImage(); // free the decoded copy before building the pyramid

    LoadedDocument doc;
    finish(doc, std::move(store), size);
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
    if (json.isNull() || m.value(QLatin1String("format")).toString() != QLatin1String("easel"))
        return failed(QObject::tr("This isn't an Easel document."));
    const int version = m.value(QLatin1String("version")).toInt();
    if (version < 1 || version > FormatVersion)
        return failed(QObject::tr("This document was saved by a newer Easel (format %1).").arg(version));
    if (m.value(QLatin1String("pixelFormat")).toString() != QLatin1String(kPixelFormat)
        || m.value(QLatin1String("tileSize")).toInt() != TileStore::TileSize)
        return failed(QObject::tr("Unsupported pixel format."));

    const QSize size(m.value(QLatin1String("width")).toInt(), m.value(QLatin1String("height")).toInt());
    const QJsonArray layers = m.value(QLatin1String("layers")).toArray();
    if (size.isEmpty() || layers.isEmpty())
        return failed(QObject::tr("The document is damaged (no canvas or layers)."));

    // One layer until M2; later layers are ignored by this version.
    const QJsonObject layer = layers.at(0).toObject();
    auto store = std::make_unique<TileStore>(QColor(0, 0, 0, 0));
    store->setDefaultPixel(pixelFromJson(layer.value(QLatin1String("default")).toArray()));

    // Qt's zip reader looks entries up by name with a linear scan, so walk the
    // entry list once rather than asking for each chunk by name.
    static const QRegularExpression chunkName(QStringLiteral("^layers/0/chunks/(-?\\d+)_(-?\\d+)$"));
    for (const QZipReader::FileInfo &info : zip.fileInfoList()) {
        if (!chunkName.match(info.filePath).hasMatch())
            continue;
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
            store->setTile({getLE<qint32>(r), getLE<qint32>(r + 4)}, tileFromData(r + 8));
        }
    }
    if (zip.status() != QZipReader::NoError)
        return failed(QObject::tr("The file can't be read completely."));

    LoadedDocument doc;
    doc.native = true;
    finish(doc, std::move(store), size);
    return doc;
}

QString saveNativeDocument(const QString &path, TileStore store, const QSize &size)
{
    // Write next to the target, then rename over it: the old file is replaced
    // in one step, or not at all. (QSaveFile would do this, but QZipWriter
    // closes its device when done, which QSaveFile doesn't allow.)
    const QFileInfo target(path);
    QTemporaryFile file(target.absoluteDir().filePath(QStringLiteral(".%1.XXXXXX.part").arg(target.fileName())));
    if (!file.open())
        return file.errorString();

    {
        QZipWriter zip(&file);
        // Tile data is noisy half-floats; deflate helps flat areas and costs time
        // on the rest. AutoCompress keeps whichever is smaller.
        zip.setCompressionPolicy(QZipWriter::NeverCompress);
        zip.addFile(QStringLiteral("mimetype"), QByteArray(kMimeType));
        zip.setCompressionPolicy(QZipWriter::AutoCompress);

        QJsonObject layer{
            {QLatin1String("name"), QLatin1String("Background")},
            {QLatin1String("visible"), true},
            {QLatin1String("opacity"), 1.0},
            {QLatin1String("blend"), QLatin1String("normal")},
            {QLatin1String("default"), pixelToJson(store.defaultPixel())},
            {QLatin1String("chunks"), QLatin1String("layers/0/chunks/")},
        };
        const QJsonObject manifest{
            {QLatin1String("format"), QLatin1String("easel")},
            {QLatin1String("version"), FormatVersion},
            {QLatin1String("width"), size.width()},
            {QLatin1String("height"), size.height()},
            {QLatin1String("tileSize"), TileStore::TileSize},
            {QLatin1String("chunkTiles"), ChunkTiles},
            {QLatin1String("pixelFormat"), QLatin1String(kPixelFormat)},
            {QLatin1String("layers"), QJsonArray{layer}},
        };
        zip.addFile(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson());

        // Preview: from the coarsest pyramid level still at least 2048 px
        // across (cheap, and already filtered in linear light), scaled to fit.
        const int side = std::max(size.width(), size.height());
        int level = 0;
        while ((side >> (level + 1)) >= PreviewMaxSide)
            ++level;
        QImage preview = flattenImage(store, size, level);
        if (std::max(preview.width(), preview.height()) > PreviewMaxSide)
            preview = preview.scaled(PreviewMaxSide, PreviewMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        preview.save(&buffer, "PNG");
        zip.addFile(QStringLiteral("preview.png"), png);

        // Group tiles into chunks: few zip entries (fast to open), each small.
        const QRect canvas(QPoint(0, 0), size);
        QHash<TileCoord, QList<TileCoord>> chunks;
        for (const TileCoord c : store.tileCoords()) {
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
                appendTileData(data, store.tile(c));
            }
            zip.addFile(chunkEntry(0, it.key().x, it.key().y), data);
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError)
            return QObject::tr("Writing the document failed.");
    }

    // Temporary files are owner-only; keep the old file's permissions, or the
    // usual ones for a new file.
    const QFile::Permissions perms = target.exists()
                                         ? QFile(path).permissions()
                                         : (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    QFile::setPermissions(file.fileName(), perms);

    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(file.fileName().toStdU16String()),
                            std::filesystem::path(path.toStdU16String()), ec);
    if (ec)
        return QString::fromStdString(ec.message());
    file.setAutoRemove(false); // it's the saved document now
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

} // namespace easel
