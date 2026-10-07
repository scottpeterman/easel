#include "paper.h"

#include <QPainter>
#include <QSet>
#include <QTest>

using namespace easeletch;

class TestPaper : public QObject
{
    Q_OBJECT

private slots:
    void papersAreWellFormed()
    {
        QSet<QString> ids;
        for (const PaperStyle &p : paperStyles()) {
            QVERIFY2(!ids.contains(p.id), qPrintable(p.id));
            ids.insert(p.id);
            QVERIFY(!p.name.isEmpty());
            QCOMPARE(p.base.alpha(), 255);
            QCOMPARE(paperStyle(p.id), &p);
        }
        for (const char *id : {"cotton", "parchment", "sepia", "coldpress", "kraft", "black"})
            QVERIFY2(paperStyle(QLatin1String(id)), id);
        QVERIFY(!paperStyle(QStringLiteral("no-such-paper")));
    }

    void aSheetIsItsColourWithASurface()
    {
        const QRect canvas(0, 0, 200, 150);
        for (const PaperStyle &p : paperStyles()) {
            TileStore store;
            fillPaper(store, canvas, p);
            double r = 0, g = 0, b = 0;
            int low = 255, high = 0;
            for (int y = 0; y < canvas.height(); ++y)
                for (int x = 0; x < canvas.width(); ++x) {
                    const QColor c = pixelToColor(store.pixel(x, y));
                    QCOMPARE(c.alpha(), 255);
                    r += c.red(), g += c.green(), b += c.blue();
                    low = qMin(low, c.lightness());
                    high = qMax(high, c.lightness());
                }
            const double n = canvas.width() * canvas.height();
            // On the whole it's the colour it says it is...
            QVERIFY2(qAbs(r / n - p.base.red()) < 6 && qAbs(g / n - p.base.green()) < 6
                         && qAbs(b / n - p.base.blue()) < 6,
                     qPrintable(p.id));
            // ... with a surface you can see, but that never shouts.
            QVERIFY2(high - low >= 6, qPrintable(p.id + QStringLiteral(" is flat")));
            QVERIFY2(high - low <= 90, qPrintable(p.id + QStringLiteral(": %1").arg(high - low)));
            // Off the canvas it's the plain colour, and opaque.
            const QColor outside = pixelToColor(store.pixel(-500, 9000));
            QVERIFY(qAbs(outside.red() - p.base.red()) <= 1 && qAbs(outside.blue() - p.base.blue()) <= 1);
            QCOMPARE(outside.alpha(), 255);
            // What the store holds is what paperColorAt says.
            const QColor at = pixelToColor(store.pixel(37, 91)), said = paperColorAt(p, 37, 91);
            QVERIFY(qAbs(at.red() - said.red()) <= 1 && qAbs(at.green() - said.green()) <= 1);
        }
    }

    void theSamePlaceIsTheSameEveryTime()
    {
        const PaperStyle &p = *paperStyle(QStringLiteral("parchment"));
        const QRect canvas(0, 0, 130, 70); // not a whole number of tiles
        TileStore a, b;
        fillPaper(a, canvas, p);
        fillPaper(b, canvas, p);
        for (int y = 0; y < 70; ++y)
            for (int x = 0; x < 130; ++x)
                QCOMPARE(pixelToColor(a.pixel(x, y)), pixelToColor(b.pixel(x, y)));
        // A bigger sheet of it is the smaller one, carried on.
        TileStore big;
        fillPaper(big, QRect(0, 0, 400, 300), p);
        QCOMPARE(pixelToColor(big.pixel(100, 50)), pixelToColor(a.pixel(100, 50)));
    }

    void aPlainSheetTakesNoMemory()
    {
        PaperStyle plain;
        plain.base = QColor(240, 230, 200);
        TileStore store;
        store.fillRect(QRect(0, 0, 100, 100), QColor(Qt::red)); // whatever was there goes
        fillPaper(store, QRect(0, 0, 2000, 1500), plain);
        QCOMPARE(store.tileCount(), 0);
        const QColor c = pixelToColor(store.pixel(50, 50));
        QVERIFY(qAbs(c.red() - 240) <= 1 && qAbs(c.green() - 230) <= 1 && qAbs(c.blue() - 200) <= 1);
    }

    void previewsShowEachPaper()
    {
        const QSize size(260, 150);
        QImage sheet(size.width() * 4 + 15, size.height() * 2 + 5, QImage::Format_RGB32);
        sheet.fill(Qt::darkGray);
        QPainter painter(&sheet);
        int i = 0;
        for (const PaperStyle &p : paperStyles()) {
            const QImage preview = paperPreview(p, size);
            QCOMPARE(preview.size(), size);
            QCOMPARE(preview.pixelColor(20, 20).rgb(), paperColorAt(p, 20, 20).rgb());
            const QPoint at((i % 4) * (size.width() + 5), (i / 4) * (size.height() + 5));
            painter.drawImage(at, preview);
            painter.setPen(p.base.lightness() < 100 ? Qt::white : Qt::black);
            painter.drawText(at + QPoint(8, 18), p.name);
            ++i;
        }
        painter.end();
        const QString dir = qEnvironmentVariable("EASELETCH_TEST_SHOTS");
        if (!dir.isEmpty())
            sheet.save(dir + QStringLiteral("/papers.png"));
    }
};

QTEST_MAIN(TestPaper)
#include "tst_paper.moc"
