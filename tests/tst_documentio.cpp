#include "documentio.h"

#include <QTemporaryDir>
#include <QTest>

using namespace easel;

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

    void reportsDecodeErrors()
    {
        const LoadedDocument doc = loadImageDocument(QStringLiteral("/no/such/file.png"));
        QVERIFY(!doc.ok());
        QVERIFY(!doc.error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDocumentIo)
#include "tst_documentio.moc"
