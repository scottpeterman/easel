#include "brush.h"
#include "documentio.h"

#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include <climits>
#include <cstring>

using namespace easeletch;

namespace {

bool sameTiles(const TileStore &a, const TileStore &b)
{
    if (a.tileCount() != b.tileCount() || !samePixel(a.defaultPixel(), b.defaultPixel()))
        return false;
    for (const TileCoord c : a.tileCoords()) {
        if (!b.hasTile(c))
            return false;
        const QImage x = a.tile(c), y = b.tile(c);
        if (std::memcmp(x.constBits(), y.constBits(), size_t(TileStore::BytesPerTile)) != 0)
            return false;
    }
    return true;
}

void paintSomething(TileStore &s, const QRect &bounds)
{
    BrushSettings b;
    b.size = 80;
    b.hardness = 0.3; // soft edges: lots of fractional values to round-trip
    b.opacity = 0.7;
    BrushStroke stroke;
    stroke.begin(&s, bounds, b, QColor(30, 120, 200), BrushMode::Paint, {{50, 50}, 1.0});
    for (int i = 1; i < 40; ++i)
        stroke.moveTo({{50.0 + i * 20, 50.0 + i * 9}, 0.3 + i / 60.0});
    stroke.end();
}

} // namespace

class TestDocumentIo : public QObject
{
    Q_OBJECT

private slots:
    void loadsTilesAndPrebuildsPyramid()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("in.png"));

        QImage img(300, 200, QImage::Format_ARGB32);
        img.fill(QColor(10, 200, 30));
        img.setPixelColor(299, 199, QColor(255, 0, 0));
        QVERIFY(img.save(path));

        const LoadedDocument doc = loadImageDocument(path);
        QVERIFY(doc.ok());
        QCOMPARE(doc.size, QSize(300, 200));
        QCOMPARE(doc.stack->layers().first().store.tileCount(), 5 * 4);
        QVERIFY(!doc.stack->layers().first().store.hasDirty());
        QCOMPARE(pixelToColor(doc.stack->layers().first().store.pixel(299, 199)), QColor(255, 0, 0));

        // Every level above the base is already computed.
        QCOMPARE(doc.pyramid.base(), &doc.stack->composite());
        QCOMPARE(doc.pyramid.topLevel(), 3);
        QCOMPARE(doc.pyramid.cachedTileCount(), 3 * 2 + 2 * 1 + 1); // levels 1, 2, 3
    }

    void nativeRoundTripIsExact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("art.easeletch"));
        const QSize size(1000, 700);

        TileStore s(Qt::white);
        paintSomething(s, QRect(QPoint(0, 0), size));
        QVERIFY(s.tileCount() > 10);
        QCOMPARE(saveNativeDocument(path, s.snapshot(), size), QString());

        const LoadedDocument doc = loadDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QVERIFY(doc.native);
        QCOMPARE(doc.size, size);
        QVERIFY(sameTiles(s, doc.stack->layers().first().store));
        QCOMPARE(doc.pyramid.base(), &doc.stack->composite());
        QVERIFY(doc.pyramid.cachedTileCount() > 0);
    }

    void transparentDefaultAndEmptyCanvas()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("empty.easeletch"));
        TileStore s(QColor(0, 0, 0, 0));
        QCOMPARE(saveNativeDocument(path, s, QSize(300, 200)), QString());
        const LoadedDocument doc = loadDocument(path);
        QVERIFY(doc.ok());
        QCOMPARE(doc.stack->layers().first().store.tileCount(), 0);
        QVERIFY(sameTiles(s, doc.stack->layers().first().store));
    }

    void tilesOutsideTheCanvasAreNotSaved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("clip.easeletch"));
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 10, 10), Qt::red);
        s.fillRect(QRect(900, 900, 10, 10), Qt::red); // outside a 200 x 200 canvas
        QCOMPARE(saveNativeDocument(path, s, QSize(200, 200)), QString());
        const LoadedDocument doc = loadDocument(path);
        QCOMPARE(doc.stack->layers().first().store.tileCount(), 1);
    }

    void previewIsFlattenedAndCapped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("wide.easeletch"));
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 5000, 500), Qt::red);
        QCOMPARE(saveNativeDocument(path, s, QSize(5000, 1000)), QString());

        QZipReader zip(path);
        QCOMPARE(zip.fileData(QStringLiteral("mimetype")), QByteArray("application/x-easeletch"));
        QImage preview;
        QVERIFY(preview.loadFromData(zip.fileData(QStringLiteral("preview.png")), "PNG"));
        QCOMPARE(preview.width(), PreviewMaxSide);
        QVERIFY(qAbs(preview.height() - 410) <= 1);
        QCOMPARE(preview.pixelColor(100, 10), QColor(Qt::red));
        QCOMPARE(preview.pixelColor(100, 300), QColor(Qt::white));
    }

    void layersRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layers.easeletch"));
        const QSize size(500, 300);

        LayerStack stack;
        stack.setSize(size);
        Layer bg;
        bg.name = QStringLiteral("Background");
        bg.store = TileStore(Qt::white);
        const int bgId = stack.insert(std::move(bg), 0, 0);
        Layer g;
        g.name = QStringLiteral("Figures");
        g.group = true;
        g.opacity = 0.75;
        const int groupId = stack.insert(std::move(g), 0, 1);
        Layer ink;
        ink.name = QStringLiteral("Ink");
        ink.blend = BlendMode::Multiply;
        ink.locked = true;
        paintSomething(ink.store, QRect(QPoint(0, 0), size));
        const int inkId = stack.insert(std::move(ink), groupId, 0);
        Layer hidden;
        hidden.name = QStringLiteral("Sketch");
        hidden.visible = false;
        hidden.opacity = 0.4;
        hidden.store.fillRect(QRect(10, 10, 100, 100), QColor(Qt::green));
        const int hiddenId = stack.insert(std::move(hidden), groupId, 1);
        stack.setActive(inkId);
        stack.recompositeAll();
        QCOMPARE(saveNativeDocument(path, stack), QString());

        const LoadedDocument doc = loadDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.size, size);
        const LayerStack &in = *doc.stack;
        QCOMPARE(in.count(), 4);
        QCOMPARE(in.activeId(), inkId);
        QCOMPARE(in.children(0), (QList<int>{bgId, groupId}));
        QCOMPARE(in.children(groupId), (QList<int>{inkId, hiddenId}));
        QVERIFY(in.layer(groupId)->group);
        QCOMPARE(in.layer(groupId)->opacity, 0.75);
        QCOMPARE(in.layer(inkId)->name, QStringLiteral("Ink"));
        QCOMPARE(in.layer(inkId)->blend, BlendMode::Multiply);
        QVERIFY(in.layer(inkId)->locked);
        QVERIFY(!in.layer(hiddenId)->visible);
        QCOMPARE(in.layer(hiddenId)->opacity, 0.4);
        for (int id : {bgId, inkId, hiddenId})
            QVERIFY(sameTiles(stack.layer(id)->store, in.layer(id)->store));
        // The composite is rebuilt and matches, and the pyramid sits on it.
        QVERIFY(sameTiles(stack.composite(), in.composite()));
        QCOMPARE(doc.pyramid.base(), &in.composite());
        QVERIFY(!in.composite().hasDirty());

        // The preview shows the flattened picture, not one layer.
        QZipReader zip(path);
        QImage preview;
        QVERIFY(preview.loadFromData(zip.fileData(QStringLiteral("preview.png")), "PNG"));
        QCOMPARE(preview.size(), size);
        QCOMPARE(preview.pixelColor(50, 50), flattenImage(stack.composite(), size).pixelColor(50, 50));
        QVERIFY(preview.pixelColor(450, 10) == QColor(Qt::white));
    }

    void masksRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("masks.easeletch"));
        const QSize size(300, 200);
        LayerStack stack;
        stack.setSize(size);
        Layer bg;
        bg.name = QStringLiteral("Background");
        bg.store = TileStore(Qt::white);
        stack.insert(std::move(bg), 0, 0);
        Layer top;
        top.name = QStringLiteral("Top");
        top.store.fillRect(QRect(0, 0, 300, 200), QColor(Qt::red));
        const int topId = stack.insert(std::move(top), 0, 1);
        stack.addMask(topId);
        paintSomething(stack.layer(topId)->mask, QRect(QPoint(0, 0), size)); // soft-edged values
        Layer off;
        off.name = QStringLiteral("Off");
        const int offId = stack.insert(std::move(off), 0, 2);
        stack.addMask(offId);
        stack.layer(offId)->mask = TileStore(Qt::black);
        stack.layer(offId)->maskEnabled = false;
        stack.recompositeAll();
        QCOMPARE(saveNativeDocument(path, stack), QString());

        const LoadedDocument doc = loadDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        const LayerStack &in = *doc.stack;
        QVERIFY(in.layer(topId)->hasMask);
        QVERIFY(in.layer(topId)->maskEnabled);
        QVERIFY(sameTiles(stack.layer(topId)->mask, in.layer(topId)->mask));
        QVERIFY(in.layer(offId)->hasMask);
        QVERIFY(!in.layer(offId)->maskEnabled);
        QVERIFY(samePixel(in.layer(offId)->mask.defaultPixel(), pixelFromColor(Qt::black)));
        QVERIFY(!in.layers().first().hasMask);
        QVERIFY(sameTiles(stack.composite(), in.composite()));
    }

    void pagesRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("pages.easeletch"));

        // Three pages of different sizes; the second has two layers and a mask.
        QList<DocumentPage> pages;
        const QSize sizes[] = {QSize(900, 500), QSize(64, 64), QSize(300, 1200)};
        for (int i = 0; i < 3; ++i) {
            TileStore store(i == 1 ? QColor(0, 0, 0, 0) : QColor(Qt::white));
            paintSomething(store, QRect(QPoint(0, 0), sizes[i]));
            LayerStack stack = LayerStack::single(std::move(store), sizes[i], QStringLiteral("Background"));
            if (i == 1) {
                Layer l;
                l.name = QStringLiteral("Ink");
                l.opacity = 0.5;
                const int id = stack.insert(std::move(l), 0, INT_MAX);
                paintSomething(stack.layer(id)->store, QRect(QPoint(0, 0), sizes[i]));
                QVERIFY(stack.addMask(id));
                stack.layer(id)->mask.writableTile({0, 0}).fill(Qt::black);
                stack.setActive(id);
            }
            stack.recompositeAll();
            pages.append({QStringLiteral("Drawing %1").arg(i + 1), std::move(stack)});
        }
        QCOMPARE(saveNativeDocument(path, pages, 1), QString());

        // Several pages are format 5, with each page's tiles under its own folder.
        {
            QZipReader zip(path);
            const QJsonObject m = QJsonDocument::fromJson(zip.fileData(QStringLiteral("manifest.json"))).object();
            QCOMPARE(m.value(QStringLiteral("version")).toInt(), FormatVersionWithoutThreshold);
            QCOMPARE(m.value(QStringLiteral("pages")).toArray().size(), 3);
            bool paged = false;
            for (const QZipReader::FileInfo &info : zip.fileInfoList())
                paged = paged || info.filePath.startsWith(QStringLiteral("pages/2/layers/0/chunks/"));
            QVERIFY(paged);
        }

        LoadedDocument doc = loadNativeDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.pages.size(), size_t(3));
        QCOMPARE(doc.activePage, 1);
        QVERIFY(!doc.pages[1].stack); // the active page is doc.stack
        QCOMPARE(doc.size, sizes[1]);
        QCOMPARE(doc.pyramid.base(), &doc.stack->composite());
        for (int i = 0; i < 3; ++i) {
            const LayerStack &in = pages.at(i).stack;
            const LayerStack &out = i == 1 ? *doc.stack : *doc.pages[size_t(i)].stack;
            QCOMPARE(doc.pages[size_t(i)].name, QStringLiteral("Drawing %1").arg(i + 1));
            QCOMPARE(out.size(), sizes[i]);
            QCOMPARE(out.count(), in.count());
            QCOMPARE(out.activeId(), in.activeId());
            for (int n = 0; n < in.count(); ++n) {
                QCOMPARE(out.layers().at(n).name, in.layers().at(n).name);
                QCOMPARE(out.layers().at(n).opacity, in.layers().at(n).opacity);
                QCOMPARE(out.layers().at(n).hasMask, in.layers().at(n).hasMask);
                QVERIFY(sameTiles(out.layers().at(n).mask, in.layers().at(n).mask));
            }
            // Every page comes back composited, ready to show.
            QCOMPARE(pixelToColor(out.composite().pixel(60, 55)), pixelToColor(in.composite().pixel(60, 55)));
        }
        // What's saved is the canvas: page 0's stroke runs past its 900 x 500.
        QVERIFY(doc.pages[0].stack->layers().at(0).store.tileCount() > 0);
    }

    void onePageIsWrittenInTheOldLayout()
    {
        // ... so builds from before pages still open documents that don't use them.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("one.easeletch"));
        QList<DocumentPage> pages;
        pages.append({QStringLiteral("Sprite"), LayerStack::single(TileStore(Qt::white), QSize(100, 80),
                                                                     QStringLiteral("Background"))});
        QCOMPARE(saveNativeDocument(path, pages, 0), QString());
        {
            QZipReader zip(path);
            const QJsonObject m = QJsonDocument::fromJson(zip.fileData(QStringLiteral("manifest.json"))).object();
            QCOMPARE(m.value(QStringLiteral("version")).toInt(), FormatVersionWithoutAdjustments);
            QVERIFY(!m.contains(QStringLiteral("pages")));
            QCOMPARE(m.value(QStringLiteral("width")).toInt(), 100);
            QCOMPARE(m.value(QStringLiteral("layers")).toArray().size(), 1);
        }
        LoadedDocument doc = loadNativeDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.pages.size(), size_t(1));
        QCOMPARE(doc.pages[0].name, QStringLiteral("Sprite"));
        QCOMPARE(doc.activePage, 0);
        QCOMPARE(doc.size, QSize(100, 80));
    }

    void pagesKeepAdjustmentLayers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto version = [](const QString &path) {
            QZipReader zip(path);
            return QJsonDocument::fromJson(zip.fileData(QStringLiteral("manifest.json")))
                .object()
                .value(QStringLiteral("version"))
                .toInt();
        };
        const auto page = [](const QString &name, bool adjusted) {
            LayerStack stack = LayerStack::single(TileStore(Qt::white), QSize(100, 80), QStringLiteral("Background"));
            if (adjusted) {
                Layer l;
                l.name = QStringLiteral("Levels");
                l.adjust = Adjustment::make(AdjustmentType::Levels);
                stack.insert(std::move(l), 0, INT_MAX);
            }
            stack.recompositeAll();
            return DocumentPage{name, std::move(stack)};
        };

        // One page with an adjustment layer: the version before pages.
        const QString one = dir.filePath(QStringLiteral("one.easeletch"));
        QCOMPARE(saveNativeDocument(one, {page(QStringLiteral("A"), true)}, 0), QString());
        QCOMPARE(version(one), FormatVersionWithoutPages);
        LoadedDocument single = loadNativeDocument(one);
        QVERIFY2(single.ok(), qPrintable(single.error));
        QVERIFY(single.stack->layers().at(1).isAdjustment());

        // Two pages, the adjustment layer on the one that isn't active.
        const QString two = dir.filePath(QStringLiteral("two.easeletch"));
        QCOMPARE(saveNativeDocument(two, {page(QStringLiteral("A"), false), page(QStringLiteral("B"), true)}, 0),
                 QString());
        QCOMPARE(version(two), FormatVersionWithoutThreshold);
        LoadedDocument doc = loadNativeDocument(two);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.stack->count(), 1);
        const LayerStack &b = *doc.pages[1].stack;
        QCOMPARE(b.count(), 2);
        QVERIFY(b.layers().at(1).isAdjustment());
        QVERIFY(b.layers().at(1).adjust == Adjustment::make(AdjustmentType::Levels));
    }

    void opensVersion1Files()
    {
        // As written before layers: one unnamed-id layer, version 1.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.easeletch"));
        const QString v1 = dir.filePath(QStringLiteral("v1.easeletch"));
        TileStore s(Qt::white);
        s.fillRect(QRect(10, 10, 50, 50), Qt::red);
        QCOMPARE(saveNativeDocument(path, s, QSize(200, 100)), QString());
        {
            QZipReader in(path);
            QZipWriter out(v1);
            for (const QZipReader::FileInfo &info : in.fileInfoList()) {
                QByteArray data = in.fileData(info.filePath);
                if (info.filePath == QLatin1String("manifest.json")) {
                    QJsonObject m = QJsonDocument::fromJson(data).object();
                    m.insert(QStringLiteral("version"), 1);
                    m.remove(QStringLiteral("activeLayer"));
                    QJsonObject layer = m.value(QStringLiteral("layers")).toArray().at(0).toObject();
                    for (const char *key : {"id", "type", "parent", "locked"})
                        layer.remove(QLatin1String(key));
                    m.insert(QStringLiteral("layers"), QJsonArray{layer});
                    data = QJsonDocument(m).toJson();
                }
                out.addFile(info.filePath, data);
            }
            out.close();
        }
        const LoadedDocument doc = loadDocument(v1);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QCOMPARE(doc.stack->count(), 1);
        QVERIFY(doc.stack->active() != nullptr);
        QVERIFY(!doc.stack->active()->group);
        QVERIFY(sameTiles(s, doc.stack->layers().first().store));
        QCOMPARE(pixelToColor(doc.stack->composite().pixel(20, 20)), QColor(Qt::red));
    }

    void opensFilesFromBeforeTheRename()
    {
        // Saved as "Easel": an .easel file whose manifest says format "easel".
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("new.easeletch"));
        const QString old = dir.filePath(QStringLiteral("old.easel"));
        TileStore s(Qt::white);
        s.fillRect(QRect(10, 10, 50, 50), Qt::red);
        QCOMPARE(saveNativeDocument(path, s, QSize(200, 100)), QString());
        {
            QZipReader in(path);
            QZipWriter out(old);
            for (const QZipReader::FileInfo &info : in.fileInfoList()) {
                QByteArray data = in.fileData(info.filePath);
                if (info.filePath == QLatin1String("manifest.json")) {
                    QJsonObject m = QJsonDocument::fromJson(data).object();
                    QCOMPARE(m.value(QStringLiteral("format")).toString(), QStringLiteral("easeletch"));
                    m.insert(QStringLiteral("format"), QStringLiteral("easel"));
                    data = QJsonDocument(m).toJson();
                }
                if (info.filePath == QLatin1String("mimetype"))
                    data = "application/x-easel";
                out.addFile(info.filePath, data);
            }
            out.close();
        }
        QVERIFY(isNativeDocument(old));
        QVERIFY(isLegacyDocument(old));
        QVERIFY(!isLegacyDocument(path));
        const LoadedDocument doc = loadDocument(old);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QVERIFY(!doc.native); // Save asks for a new .easeletch name
        QVERIFY(sameTiles(s, doc.stack->layers().first().store));
        QVERIFY(loadDocument(path).native);
    }

    void rejectsNewerAndForeignFiles()
    {
        QTemporaryDir dir;
        const QString newer = dir.filePath(QStringLiteral("newer.easeletch"));
        {
            QZipWriter zip(newer);
            const QJsonObject m{{QStringLiteral("format"), QStringLiteral("easeletch")},
                                {QStringLiteral("version"), FormatVersion + 1}};
            zip.addFile(QStringLiteral("manifest.json"), QJsonDocument(m).toJson());
            zip.close();
        }
        LoadedDocument doc = loadDocument(newer);
        QVERIFY(!doc.ok());
        QVERIFY(doc.error.contains(QStringLiteral("newer")));

        const QString junk = dir.filePath(QStringLiteral("junk.easeletch"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a zip");
        f.close();
        doc = loadDocument(junk);
        QVERIFY(!doc.ok());
    }

    void saveErrorsAreReported()
    {
        TileStore s(Qt::white);
        const QString error = saveNativeDocument(QStringLiteral("/no/such/dir/x.easeletch"), s, QSize(10, 10));
        QVERIFY(!error.isEmpty());
    }

    void failedSaveLeavesTheOldFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("keep.easeletch"));
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 10, 10), Qt::blue);
        QCOMPARE(saveNativeDocument(path, s, QSize(100, 100)), QString());
        QByteArray before;
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            before = f.readAll();
        }

        // Make the directory read-only so the replacement can't be created.
        QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner);
        s.fillRect(QRect(0, 0, 10, 10), Qt::green);
        const QString error = saveNativeDocument(path, s, QSize(100, 100));
        QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (error.isEmpty())
            QSKIP("Filesystem permissions not enforced here (running as root?)");

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void exportsFlattenedImages()
    {
        QTemporaryDir dir;
        TileStore s(QColor(0, 0, 0, 0));
        s.fillRect(QRect(0, 0, 50, 100), QColor(200, 30, 60));
        const QSize size(100, 100);

        const QString png = dir.filePath(QStringLiteral("out.png"));
        QCOMPARE(exportImage(png, s, size), QString());
        const QImage a(png);
        QCOMPARE(a.size(), size);
        QCOMPARE(a.pixelColor(10, 10), QColor(200, 30, 60));
        QCOMPARE(a.pixelColor(90, 10).alpha(), 0); // transparent stays transparent

        const QString jpg = dir.filePath(QStringLiteral("out.jpg"));
        QCOMPARE(exportImage(jpg, s, size), QString());
        const QImage b(jpg);
        const QColor white = b.pixelColor(90, 50); // composited over white
        QVERIFY(white.red() > 245 && white.green() > 245 && white.blue() > 245);
    }

    void openingAnImageIsNotNative()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pic.png"));
        QImage img(20, 20, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(path));
        const LoadedDocument doc = loadDocument(path);
        QVERIFY(doc.ok());
        QVERIFY(!doc.native);
    }

    void reportsDecodeErrors()
    {
        const LoadedDocument doc = loadImageDocument(QStringLiteral("/no/such/file.png"));
        QVERIFY(!doc.ok());
        QVERIFY(!doc.error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDocumentIo)
#include "tst_documentio.moc"
