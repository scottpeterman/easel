#include "tilestore.h"

#include <QTest>

using namespace easel;

namespace {

bool isColor(const Pixel &p, const QColor &c)
{
    return samePixel(p, pixelFromColor(c));
}

} // namespace

class TestTileStore : public QObject
{
    Q_OBJECT

private slots:
    void emptyStoreReadsDefault()
    {
        TileStore s(Qt::white);
        QCOMPARE(s.tileCount(), 0);
        QCOMPARE(s.memoryBytes(), 0);
        QVERIFY(isColor(s.pixel(123456, -98765), Qt::white));
    }

    void tileAddressingHandlesNegatives()
    {
        QCOMPARE(TileStore::tileAt(0, 0), (TileCoord{0, 0}));
        QCOMPARE(TileStore::tileAt(63, 63), (TileCoord{0, 0}));
        QCOMPARE(TileStore::tileAt(64, 0), (TileCoord{1, 0}));
        QCOMPARE(TileStore::tileAt(-1, -1), (TileCoord{-1, -1}));
        QCOMPARE(TileStore::tileAt(-64, -65), (TileCoord{-1, -2}));
    }

    void fillAcrossTileBoundary()
    {
        TileStore s;
        s.fillRect(QRect(60, 60, 10, 10), Qt::red);
        QCOMPARE(s.tileCount(), 4);
        QVERIFY(isColor(s.pixel(60, 60), Qt::red));
        QVERIFY(isColor(s.pixel(69, 69), Qt::red));
        QVERIFY(isColor(s.pixel(70, 70), QColor(0, 0, 0, 0)));
        QVERIFY(isColor(s.pixel(59, 60), QColor(0, 0, 0, 0)));
    }

    void onlyTouchedTilesAllocate()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(5000, 5000, 1, 1), Qt::black);
        QCOMPARE(s.tileCount(), 1);
        QCOMPARE(s.memoryBytes(), TileStore::BytesPerTile);
    }

    void fillingWithDefaultFreesWholeTiles()
    {
        TileStore s(Qt::white);
        s.fillRect(QRect(0, 0, 128, 64), Qt::blue);
        QCOMPARE(s.tileCount(), 2);
        s.fillRect(QRect(0, 0, 64, 64), Qt::white);
        QCOMPARE(s.tileCount(), 1);
        QVERIFY(!s.hasTile({0, 0}));
        QVERIFY(s.hasTile({1, 0}));
    }

    void dirtyTracking()
    {
        TileStore s;
        s.fillRect(QRect(0, 0, 65, 1), Qt::green);
        const auto dirty = s.takeDirty();
        QCOMPARE(dirty.size(), 2);
        QVERIFY(dirty.contains({0, 0}));
        QVERIFY(dirty.contains({1, 0}));
        QVERIFY(!s.hasDirty());

        s.clear();
        QCOMPARE(s.takeDirty().size(), 2);
        QCOMPARE(s.tileCount(), 0);
    }

    void snapshotIsCopyOnWrite()
    {
        TileStore s;
        s.fillRect(QRect(0, 0, 64, 64), Qt::red);
        const TileStore snap = s.snapshot();

        // Shared until written.
        QCOMPARE(snap.tile({0, 0}).constBits(), s.tile({0, 0}).constBits());

        s.fillRect(QRect(0, 0, 1, 1), Qt::blue);
        QVERIFY(isColor(s.pixel(0, 0), Qt::blue));
        QVERIFY(isColor(snap.pixel(0, 0), Qt::red));
        QVERIFY(snap.tile({0, 0}).constBits() != s.tile({0, 0}).constBits());
    }

    void writeImageAtOffset()
    {
        QImage img(3, 2, QImage::Format_ARGB32);
        img.fill(QColor(0, 255, 0));
        img.setPixelColor(0, 0, QColor(255, 0, 0, 0));

        TileStore s(Qt::white);
        s.writeImage(img, QPoint(-1, 63));
        QCOMPARE(s.tileCount(), 4);
        QVERIFY(isColor(s.pixel(0, 63), QColor(0, 255, 0)));
        QVERIFY(isColor(s.pixel(1, 64), QColor(0, 255, 0)));
        QCOMPARE(float(s.pixel(-1, 63).a), 0.0f);
        QVERIFY(isColor(s.pixel(2, 63), Qt::white));
    }

    void displayConversion()
    {
        TileStore s;
        s.fillRect(QRect(0, 0, 64, 64), QColor(10, 128, 250));
        const QImage d = TileStore::toDisplay(s.tile({0, 0}));
        QCOMPARE(d.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(d.size(), QSize(64, 64));
        const QRgb px = d.pixel(31, 31);
        QVERIFY(qAbs(qRed(px) - 10) <= 1);
        QVERIFY(qAbs(qGreen(px) - 128) <= 1);
        QVERIFY(qAbs(qBlue(px) - 250) <= 1);
    }
};

QTEST_GUILESS_MAIN(TestTileStore)
#include "tst_tilestore.moc"
