#include "brush.h"
#include "documentio.h"

#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include <cstring>

using namespace easel;

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
        QCOMPARE(doc.store->tileCount(), 5 * 4);
        QVERIFY(!doc.store->hasDirty());
        QCOMPARE(pixelToColor(doc.store->pixel(299, 199)), QColor(255, 0, 0));

        // Every level above the base is already computed.
        QCOMPARE(doc.pyramid.base(), doc.store.get());
        QCOMPARE(doc.pyramid.topLevel(), 3);
        QCOMPARE(doc.pyramid.cachedTileCount(), 3 * 2 + 2 * 1 + 1); // levels 1, 2, 3
    }

    void nativeRoundTripIsExact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("art.easel"));
        const QSize size(1000, 700);

        TileStore s(Qt::white);
        paintSomething(s, QRect(QPoint(0, 0), size));
        QVERIFY(s.tileCount() > 10);
        QCOMPARE(saveNativeDocument(path, s.snapshot(), size), QString());

        const LoadedDocument doc = loadDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        QVERIFY(doc.native);
        QCOMPARE(doc.size, size);
        QVERIFY(sameTiles(s, *doc.store));
        QCOMPARE(doc.pyramid.base(), doc.store.get());
        QVERIFY(doc.pyramid.cachedTileCount() > 0);
    }

    void transparentDefaultAndEmptyCanvas()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("empty.easel"));
        TileStore s(QColor(0, 0, 0, 0));
        QCOMPARE(saveNativeDocument(path, s, QSize(300, 200)), QString());
        const LoadedDocument doc = loadDocument(path);
        QVERIFY(doc.ok());
        QCOMPARE(doc.store->tileCount(), 0);
        QVERIFY(sameTiles(s, *doc.store));
    }

    void tilesOutsideTheCanvasAreNotSaved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("clip.easel"));
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 10, 10), Qt::red);
        s.fillRect(QRect(900, 900, 10, 10), Qt::red); // outside a 200 x 200 canvas
        QCOMPARE(saveNativeDocument(path, s, QSize(200, 200)), QString());
        const LoadedDocument doc = loadDocument(path);
        QCOMPARE(doc.store->tileCount(), 1);
    }

    void previewIsFlattenedAndCapped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("wide.easel"));
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 5000, 500), Qt::red);
        QCOMPARE(saveNativeDocument(path, s, QSize(5000, 1000)), QString());

        QZipReader zip(path);
        QCOMPARE(zip.fileData(QStringLiteral("mimetype")), QByteArray("application/x-easel"));
        QImage preview;
        QVERIFY(preview.loadFromData(zip.fileData(QStringLiteral("preview.png")), "PNG"));
        QCOMPARE(preview.width(), PreviewMaxSide);
        QVERIFY(qAbs(preview.height() - 410) <= 1);
        QCOMPARE(preview.pixelColor(100, 10), QColor(Qt::red));
        QCOMPARE(preview.pixelColor(100, 300), QColor(Qt::white));
    }

    void rejectsNewerAndForeignFiles()
    {
        QTemporaryDir dir;
        const QString newer = dir.filePath(QStringLiteral("newer.easel"));
        {
            QZipWriter zip(newer);
            const QJsonObject m{{QStringLiteral("format"), QStringLiteral("easel")},
                                {QStringLiteral("version"), FormatVersion + 1}};
            zip.addFile(QStringLiteral("manifest.json"), QJsonDocument(m).toJson());
            zip.close();
        }
        LoadedDocument doc = loadDocument(newer);
        QVERIFY(!doc.ok());
        QVERIFY(doc.error.contains(QStringLiteral("newer")));

        const QString junk = dir.filePath(QStringLiteral("junk.easel"));
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
        const QString error = saveNativeDocument(QStringLiteral("/no/such/dir/x.easel"), s, QSize(10, 10));
        QVERIFY(!error.isEmpty());
    }

    void failedSaveLeavesTheOldFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("keep.easel"));
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
