#include "documentio.h"
#include "layerstack.h"
#include "textrender.h"
#include "tilestore.h"

#include <QFontMetricsF>
#include <QTemporaryDir>
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

    void wordsWrapInsideABox()
    {
        TextSettings s;
        s.text = QStringLiteral("one two three four five six seven eight");
        s.pixelSize = 30;
        int wide = 0, boxed = 0;
        const QImage oneLine = renderText(s, nullptr, &wide);
        QVERIFY(wide > 300);
        s.boxWidth = 200;
        const QImage wrapped = renderText(s, nullptr, &boxed);
        QCOMPARE(boxed, 200); // alignment is against the box
        QVERIFY(wrapped.width() < oneLine.width());
        QVERIFY(wrapped.height() > oneLine.height() * 2);
        // No ink past the box (plus the image's own margin).
        QPoint inset;
        renderText(s, &inset);
        for (int y = 0; y < wrapped.height(); ++y)
            for (int x = inset.x() + 200 + 4; x < wrapped.width(); ++x)
                QVERIFY(float(reinterpret_cast<const Pixel *>(wrapped.constScanLine(y))[x].a) < 0.5f);
        // A line break typed by hand still breaks.
        s.text = QStringLiteral("a\nb");
        TextSettings unboxed = s;
        unboxed.boxWidth = 0;
        QCOMPARE(renderText(s).height(), renderText(unboxed).height());
        // A word wider than the box isn't cut: it sticks out.
        s.text = QStringLiteral("Supercalifragilistic");
        s.boxWidth = 40;
        int w = 0;
        QVERIFY(!renderText(s, nullptr, &w).isNull());
        QVERIFY(w > 40);
    }

    void outlineGoesRoundTheLetters()
    {
        TextSettings s;
        s.text = QStringLiteral("MEME");
        s.pixelSize = 80;
        s.bold = true;
        s.color = Qt::white;
        const QImage plain = renderText(s);
        s.outline = 5;
        s.outlineColor = Qt::black;
        QPoint inset;
        const QImage lined = renderText(s, &inset);
        QCOMPARE(lined.width(), plain.width() + 10);
        QCOMPARE(lined.height(), plain.height() + 10);
        int white = 0, black = 0;
        for (int y = 0; y < lined.height(); ++y) {
            const auto *row = reinterpret_cast<const Pixel *>(lined.constScanLine(y));
            for (int x = 0; x < lined.width(); ++x) {
                if (float(row[x].a) < 0.999f)
                    continue;
                const QColor c = pixelToColor(row[x]);
                white += c.red() > 250 && c.green() > 250 && c.blue() > 250;
                black += c.red() < 5 && c.green() < 5 && c.blue() < 5;
            }
        }
        QVERIFY(white > 1000);
        QVERIFY(black > 1000);
        // The edge of the image is clear: the line fits inside it.
        for (int x = 0; x < lined.width(); ++x) {
            QCOMPARE(float(reinterpret_cast<const Pixel *>(lined.constScanLine(0))[x].a), 0.0f);
            QCOMPARE(float(reinterpret_cast<const Pixel *>(lined.constScanLine(lined.height() - 1))[x].a), 0.0f);
        }
        // Hard text with an outline: two colours, nothing in between.
        s.smooth = false;
        const QImage hard = renderText(s);
        for (int y = 0; y < hard.height(); ++y) {
            const auto *row = reinterpret_cast<const Pixel *>(hard.constScanLine(y));
            for (int x = 0; x < hard.width(); ++x) {
                QVERIFY(float(row[x].a) == 0.0f || float(row[x].a) == 1.0f);
                if (float(row[x].a) == 1.0f) {
                    const int v = pixelToColor(row[x]).red();
                    QVERIFY(v == 0 || v == 255);
                }
            }
        }
    }

    void settingsSurviveJson()
    {
        TextSettings s;
        s.text = QStringLiteral("Two\nlines \u00e9");
        s.family = QStringLiteral("Some Font");
        s.pixelSize = 73;
        s.bold = true;
        s.italic = true;
        s.align = Qt::AlignHCenter;
        s.smooth = false;
        s.color = QColor(10, 200, 30, 128);
        s.outline = 7;
        s.outlineColor = QColor(1, 2, 3);
        s.boxWidth = 321;
        const TextSettings r = TextSettings::fromJson(s.toJson());
        QCOMPARE(r.text, s.text);
        QCOMPARE(r.family, s.family);
        QCOMPARE(r.pixelSize, 73);
        QVERIFY(r.bold && r.italic && !r.smooth);
        QCOMPARE(r.align, Qt::Alignment(Qt::AlignHCenter));
        QCOMPARE(r.color, s.color);
        QCOMPARE(r.outline, 7);
        QCOMPARE(r.outlineColor, s.outlineColor);
        QCOMPARE(r.boxWidth, 321);
        // Nonsense in a file is brought into range, not trusted.
        QJsonObject bad = s.toJson();
        bad.insert(QLatin1String("size"), 999999);
        bad.insert(QLatin1String("outline"), -4);
        bad.insert(QLatin1String("align"), QStringLiteral("sideways"));
        const TextSettings b = TextSettings::fromJson(bad);
        QCOMPARE(b.pixelSize, TextSettings::MaxSize);
        QCOMPARE(b.outline, 0);
        QCOMPARE(b.align, Qt::Alignment(Qt::AlignLeft));
    }

    void aTextLayerIsTextWhileItsPixelsAreItsOwn()
    {
        const QRect canvas(0, 0, 400, 300);
        Layer l;
        l.id = 2;
        l.name = QStringLiteral("Words");
        QVERIFY(!l.isText());
        l.hasText = true;
        l.text.text = QStringLiteral("Hello");
        l.text.pixelSize = 50;
        l.textAnchor = QPoint(30, 40);
        drawTextLayer(l, canvas);
        QVERIFY(l.isText());
        QVERIFY(l.store.tileCount() > 0);
        QCOMPARE(l.textBox.topLeft().x(), 30);
        QVERIFY(l.textBox.height() >= 50);

        // A copy is text too; touch a pixel and it isn't; put it back and it is.
        Layer copy = l;
        QVERIFY(copy.isText());
        const TileCoord c = l.store.tileCoords().first();
        const QImage original = l.store.tile(c);
        reinterpret_cast<Pixel *>(l.store.writableTile(c).scanLine(3))[3] = makePixel(1, 0, 0, 1);
        QVERIFY(!l.isText());
        QVERIFY(copy.isText());
        l.store.setTile(c, original);
        QVERIFY(l.isText());
        // The same pixels in a tile of their own still count.
        QImage same = original.copy();
        l.store.setTile(c, same);
        QVERIFY(l.isText());
        // A tile it never had, or one gone, doesn't.
        l.store.fillRect(QRect(390, 290, 5, 5), QColor(Qt::red));
        QVERIFY(!l.isText());
        drawTextLayer(l, canvas);
        QVERIFY(l.isText());
        // Nothing is kept outside the canvas.
        l.textAnchor = QPoint(380, 40);
        drawTextLayer(l, canvas);
        for (const TileCoord t : l.store.tileCoords())
            QVERIFY(TileStore::tileRect(t).intersects(canvas));
        l.textAnchor = QPoint(30, 40);
        drawTextLayer(l, canvas);

        // Saved and opened again it's still text; painted on first, it's saved as pixels.
        QTemporaryDir dir;
        LayerStack stack = LayerStack::single(TileStore(QColor(Qt::white)), canvas.size(), QStringLiteral("Background"));
        const int text = stack.insert(l, 0, INT_MAX);
        Layer painted = l;
        painted.id = 0;
        painted.name = QStringLiteral("Painted");
        painted.store.fillRect(QRect(0, 0, 10, 10), QColor(Qt::red));
        const int flat = stack.insert(painted, 0, INT_MAX);
        stack.recompositeAll();
        const QString path = dir.filePath(QStringLiteral("t.easeletch"));
        QCOMPARE(saveNativeDocument(path, stack), QString());
        const LoadedDocument doc = loadNativeDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        const Layer *back = doc.stack->layer(text);
        QVERIFY(back && back->isText());
        QCOMPARE(back->text.text, QStringLiteral("Hello"));
        QCOMPARE(back->text.pixelSize, 50);
        QCOMPARE(back->textAnchor, QPoint(30, 40));
        QCOMPARE(back->textBox, l.textBox);
        QVERIFY(doc.stack->layer(flat) && !doc.stack->layer(flat)->isText());
        QVERIFY(!doc.stack->layer(flat)->hasText);
        QCOMPARE(doc.stack->layer(flat)->store.tileCount(), painted.store.tileCount());
    }
};

QTEST_MAIN(TestText)
#include "tst_text.moc"
