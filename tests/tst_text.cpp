#include "textrender.h"
#include "tilestore.h"

#include <QTest>

using namespace easeletch;

// Text is drawn with the fonts installed on the machine, so this runs on the
// real window system, not the offscreen one (which, on Windows, has no fonts).
class TestText : public QObject
{
    Q_OBJECT

private slots:
    void textRendersAsPixelsInItsColour()
    {
        TextSettings s;
        s.text = QStringLiteral("Hi");
        s.pixelSize = 60;
        s.color = QColor(200, 30, 30);
        QPoint inset;
        int width = 0;
        const QImage img = renderText(s, &inset, &width);
        QVERIFY(!img.isNull());
        QCOMPARE(img.format(), TileStore::TileFormat);
        QVERIFY(width > 20 && width < 200);
        QVERIFY(img.width() >= width + 2 * inset.x());
        QVERIFY(img.height() >= 60);

        // Some pixels are fully the colour, some are empty, and smooth text
        // has in-between ones along the edges.
        int solid = 0, soft = 0, empty = 0;
        bool colourOk = true;
        for (int y = 0; y < img.height(); ++y) {
            const auto *row = reinterpret_cast<const Pixel *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const float a = float(row[x].a);
                if (a <= 0.0f) {
                    ++empty;
                } else if (a >= 0.999f) {
                    ++solid;
                    const QColor c = pixelToColor(row[x]);
                    colourOk = colourOk && qAbs(c.red() - 200) <= 1 && qAbs(c.green() - 30) <= 1;
                } else {
                    ++soft;
                }
            }
        }
        QVERIFY(solid > 100);
        QVERIFY(soft > 20);
        QVERIFY(empty > solid);
        QVERIFY(colourOk);
        // The corners of the image are clear: the letters sit inside the inset.
        QCOMPARE(float(reinterpret_cast<const Pixel *>(img.constScanLine(0))[0].a), 0.0f);

        // Hard text has no in-between pixels.
        s.smooth = false;
        const QImage hard = renderText(s);
        for (int y = 0; y < hard.height(); ++y) {
            const auto *row = reinterpret_cast<const Pixel *>(hard.constScanLine(y));
            for (int x = 0; x < hard.width(); ++x)
                QVERIFY(float(row[x].a) == 0.0f || float(row[x].a) == 1.0f);
        }
    }

    void textSizeLinesAndStyleChangeTheImage()
    {
        TextSettings s;
        s.text = QStringLiteral("Easeletch");
        s.pixelSize = 40;
        int w40 = 0, w80 = 0, wBold = 0, wTwo = 0;
        const QImage a = renderText(s, nullptr, &w40);
        s.pixelSize = 80;
        const QImage b = renderText(s, nullptr, &w80);
        QVERIFY(w80 > w40 * 3 / 2);
        QVERIFY(b.height() > a.height() * 3 / 2);
        s.pixelSize = 40;
        s.bold = true;
        renderText(s, nullptr, &wBold);
        QVERIFY(wBold >= w40);
        s.bold = false;
        s.text = QStringLiteral("Easeletch\nan editor");
        const QImage two = renderText(s, nullptr, &wTwo);
        QCOMPARE(wTwo, w40); // the longest line sets the width
        QVERIFY(two.height() > a.height() * 3 / 2);

        // Alignment moves the short line by the difference between the two
        // lines' widths (measured, so this holds for any font): all of it for
        // right, half of it for centre.
        const auto inkLeft = [](const QImage &img, int y0, int y1) {
            for (int x = 0; x < img.width(); ++x)
                for (int y = y0; y < y1; ++y)
                    if (float(reinterpret_cast<const Pixel *>(img.constScanLine(y))[x].a) > 0.5f)
                        return x;
            return -1;
        };
        int wShort = 0;
        s.text = QStringLiteral("it");
        renderText(s, nullptr, &wShort);
        const int slack = w40 - wShort;
        QVERIFY(slack > 20);
        s.text = QStringLiteral("Easeletch\nit");
        const QImage leftImg = renderText(s);
        // The second line's rows: the lower 40% of a two-line image.
        const int lower = leftImg.height() * 6 / 10;
        const int left = inkLeft(leftImg, lower, leftImg.height());
        QVERIFY(left >= 0);
        s.align = Qt::AlignRight;
        const QImage rightImg = renderText(s);
        QVERIFY(qAbs(inkLeft(rightImg, lower, rightImg.height()) - (left + slack)) <= 3);
        s.align = Qt::AlignHCenter;
        const QImage centreImg = renderText(s);
        QVERIFY(qAbs(inkLeft(centreImg, lower, centreImg.height()) - (left + slack / 2)) <= 3);

        // Nothing to draw.
        s.text = QStringLiteral("  \n ");
        QVERIFY(renderText(s).isNull());
        s.text.clear();
        QVERIFY(renderText(s).isNull());
        // A font that isn't installed falls back to one that is.
        QVERIFY(!resolvedFontFamily(QStringLiteral("No Such Font 12345")).isEmpty());
    }
};

QTEST_MAIN(TestText)
#include "tst_text.moc"
