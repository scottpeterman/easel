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

    void framesGoRoundTheWordsWithoutMovingThem()
    {
        TextSettings s;
        s.text = QStringLiteral("A card\nwith two lines");
        s.pixelSize = 40;
        s.color = Qt::black;
        QPoint plainInset;
        int plainWidth = 0;
        QRect none(1, 1, 1, 1);
        const QImage plain = renderText(s, &plainInset, &plainWidth, &none);
        QVERIFY(none.isEmpty());
        const TextLayout plainLayout = layoutText(s, QPoint(500, 300));

        const QColor red(200, 0, 0), cream(250, 240, 200);
        const auto count = [](const QImage &img, const QRect &area, const QColor &want) {
            int n = 0;
            for (int y = std::max(0, area.top()); y <= std::min(img.height() - 1, area.bottom()); ++y) {
                const auto *row = reinterpret_cast<const Pixel *>(img.constScanLine(y));
                for (int x = std::max(0, area.left()); x <= std::min(img.width() - 1, area.right()); ++x) {
                    const QColor c = pixelToColor(row[x]);
                    n += float(row[x].a) > 0.99f && qAbs(c.red() - want.red()) <= 2
                         && qAbs(c.green() - want.green()) <= 2 && qAbs(c.blue() - want.blue()) <= 2;
                }
            }
            return n;
        };
        for (int i = 1; i < FrameStyleCount; ++i) {
            s.frame = TextFrame();
            s.frame.style = FrameStyle(i);
            s.frame.line = 4;
            s.frame.lineColor = red;
            s.frame.padding = 20;
            QPoint inset;
            int width = 0;
            QRect frame;
            const QImage img = renderText(s, &inset, &width, &frame);
            QVERIFY2(!img.isNull(), qPrintable(frameStyleKey(s.frame.style)));
            QCOMPARE(width, plainWidth);
            QVERIFY(img.width() > plain.width());
            QVERIFY(img.height() > plain.height());
            // The frame's area is inside the image and reaches past the padding on every side.
            QVERIFY(QRect(QPoint(0, 0), img.size()).contains(frame));
            QVERIFY(frame.left() <= inset.x() - 20 && frame.right() >= inset.x() + width + 20 - 1);
            QVERIFY(frame.top() <= inset.y() - 20);
            // Its line is drawn, in its colour, and nothing is drawn outside its area.
            QVERIFY2(count(img, frame, red) > 200, qPrintable(frameStyleKey(s.frame.style)));
            int outside = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (!frame.contains(x, y))
                        outside += float(reinterpret_cast<const Pixel *>(img.constScanLine(y))[x].a) > 0.0f;
            QCOMPARE(outside, 0);
            // No fill: the middle of the padding is clear. Filled: it's the fill colour.
            const QRect gap(inset.x() - 12, inset.y() + 30, 6, 6);
            QCOMPARE(count(img, gap, cream), 0);
            QCOMPARE(float(reinterpret_cast<const Pixel *>(img.constScanLine(gap.top()))[gap.left()].a), 0.0f);
            s.frame.filled = true;
            s.frame.fill = cream;
            const QImage filled = renderText(s);
            QCOMPARE(filled.size(), img.size());
            QCOMPARE(count(filled, gap, cream), 36);
            // The words are where they were: the frame goes on round them.
            const TextLayout layout = layoutText(s, QPoint(500, 300));
            QCOMPARE(layout.origin + inset, plainLayout.origin + plainInset);
            QVERIFY(layout.box.contains(plainLayout.box.adjusted(0, plainInset.y(), 0, -plain.height() / 3)));
            QVERIFY(layout.box.width() >= plainWidth + 40);
            // Hard text: every pixel is whole.
            s.smooth = false;
            const QImage hard = renderText(s);
            for (int y = 0; y < hard.height(); ++y)
                for (int x = 0; x < hard.width(); ++x) {
                    const float a = float(reinterpret_cast<const Pixel *>(hard.constScanLine(y))[x].a);
                    QVERIFY(a == 0.0f || a == 1.0f);
                }
            s.smooth = true;
        }

        // A wider box makes a wider frame; more padding a bigger one.
        s.frame = TextFrame();
        s.frame.style = FrameStyle::Single;
        QRect narrow, wide, roomy;
        renderText(s, nullptr, nullptr, &narrow);
        s.boxWidth = 600;
        renderText(s, nullptr, nullptr, &wide);
        QVERIFY(wide.width() >= 600 + 2 * s.frame.padding);
        QVERIFY(wide.width() > narrow.width());
        s.frame.padding = 60;
        renderText(s, nullptr, nullptr, &roomy);
        QCOMPARE(roomy.width(), wide.width() + 2 * (60 - 16));

        // Frames survive the file; text without one writes none.
        s.frame.style = FrameStyle::Looped;
        s.frame.line = 7;
        s.frame.lineColor = red;
        s.frame.filled = true;
        s.frame.fill = cream;
        const TextSettings r = TextSettings::fromJson(s.toJson());
        QCOMPARE(r.frame.style, FrameStyle::Looped);
        QCOMPARE(r.frame.line, 7);
        QCOMPARE(r.frame.lineColor, red);
        QVERIFY(r.frame.filled);
        QCOMPARE(r.frame.fill, cream);
        QCOMPARE(r.frame.padding, 60);
        s.frame.style = FrameStyle::None;
        QVERIFY(!s.toJson().contains(QLatin1String("frame")));
        QVERIFY(!TextSettings::fromJson(s.toJson()).frame.isActive());
        // A stencil this build doesn't know reads as no frame, not as garbage.
        QCOMPARE(frameStyleFromKey(QStringLiteral("starburst")), FrameStyle::None);
        for (int i = 1; i < FrameStyleCount; ++i)
            QCOMPARE(frameStyleFromKey(frameStyleKey(FrameStyle(i))), FrameStyle(i));
    }

    void balloonsHaveTailsThatPointWhereTheyAreTold()
    {
        TextSettings s;
        s.text = QStringLiteral("Seriously,\nI got this.");
        s.pixelSize = 36;
        s.color = Qt::black;
        s.align = Qt::AlignHCenter;
        s.frame.line = 4;
        s.frame.lineColor = Qt::black;
        s.frame.filled = true;
        s.frame.fill = Qt::white;
        const auto solid = [](const QImage &img, const QPoint &p) {
            return QRect(QPoint(0, 0), img.size()).contains(p)
                   && float(reinterpret_cast<const Pixel *>(img.constScanLine(p.y()))[p.x()].a) > 0.99f;
        };
        for (const FrameStyle style : {FrameStyle::Speech, FrameStyle::Whisper, FrameStyle::Thought, FrameStyle::Shout,
                                       FrameStyle::Rounded}) {
            const QByteArray name = frameStyleKey(style).toLatin1();
            s.frame.style = style;
            s.frame.tail = false;
            TextMetrics bare;
            const QImage without = renderText(s, &bare);
            QVERIFY2(!without.isNull(), name.constData());
            QVERIFY(!bare.hasTail);
            // The words sit on the fill, in the middle of the body.
            QVERIFY2(solid(without, bare.centre), name.constData());
            QVERIFY(bare.frame.contains(bare.centre));

            // A tail aimed down and to the right: the image grows that way
            // only, the body and the words stay as they were, and something
            // is drawn at the tip.
            s.frame.tail = true;
            s.frame.tailOffset = QPoint(200, 260);
            TextMetrics m;
            const QImage with = renderText(s, &m);
            QVERIFY2(m.hasTail, name.constData());
            QCOMPARE(m.tailTip, m.centre + QPoint(200, 260));
            QCOMPARE(m.inset, bare.inset); // nothing added above or to the left
            QCOMPARE(m.frame, bare.frame);
            QVERIFY(with.width() > without.width());
            QVERIFY(with.height() > without.height() + 100);
            QVERIFY(QRect(QPoint(0, 0), with.size()).contains(m.tailTip));
            bool nearTip = false;
            for (int dy = -8; dy <= 2 && !nearTip; ++dy)
                for (int dx = -8; dx <= 2 && !nearTip; ++dx)
                    nearTip = solid(with, m.tailTip + QPoint(dx, dy));
            QVERIFY2(nearTip, name.constData());

            // Aimed up and to the left, the image grows there instead, and
            // the words are still where the layout says they are.
            s.frame.tailOffset = QPoint(-300, -220);
            const TextLayout up = layoutText(s, QPoint(1000, 1000));
            s.frame.tail = false;
            const TextLayout plain = layoutText(s, QPoint(1000, 1000));
            QCOMPARE(up.centre, plain.centre);
            QCOMPARE(up.box, plain.box);
            QVERIFY(up.hasTail && !plain.hasTail);
            QCOMPARE(up.tailTip, up.centre + QPoint(-300, -220));
            QVERIFY(up.origin.x() < plain.origin.x() && up.origin.y() < plain.origin.y());
            QVERIFY(QRect(up.origin, up.image.size()).contains(up.tailTip));

            // With no place given it points somewhere sensible: below the body.
            s.frame.tail = true;
            s.frame.tailOffset = QPoint();
            TextMetrics d;
            renderText(s, &d);
            QVERIFY(d.hasTail);
            QVERIFY(d.tailTip.y() > d.frame.bottom());
            QVERIFY(d.frame.left() < d.tailTip.x() && d.tailTip.x() < d.frame.right());

            // Aimed inside the body there's nothing to draw, but it's still a
            // tail (its handle is there to drag out again).
            s.frame.tailOffset = QPoint(3, 3);
            TextMetrics in;
            const QImage inside = renderText(s, &in);
            QVERIFY(in.hasTail);
            QCOMPARE(inside.size(), without.size());
        }

        // The stencil frames have no tails, whatever the setting says.
        s.frame.style = FrameStyle::Double;
        s.frame.tail = true;
        s.frame.tailOffset = QPoint(200, 260);
        TextMetrics none;
        renderText(s, &none);
        QVERIFY(!none.hasTail);
        QVERIFY(!s.frame.hasTail());
        QVERIFY(!s.toJson().value(QLatin1String("frame")).toObject().contains(QLatin1String("tail")));

        // A whisper's line is dashed: there are gaps in it a speech balloon's doesn't have.
        s.frame.tail = false;
        s.frame.filled = false;
        const auto lineInk = [&](FrameStyle style) {
            s.frame.style = style;
            const QImage img = renderText(s);
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    n += float(reinterpret_cast<const Pixel *>(img.constScanLine(y))[x].a) > 0.99f;
            return n;
        };
        const int speech = lineInk(FrameStyle::Speech);
        const int whisper = lineInk(FrameStyle::Whisper);
        QVERIFY(whisper < speech);

        // The tail survives the file.
        s.frame.style = FrameStyle::Thought;
        s.frame.tail = true;
        s.frame.tailOffset = QPoint(-40, 175);
        const TextSettings r = TextSettings::fromJson(s.toJson());
        QCOMPARE(r.frame.style, FrameStyle::Thought);
        QVERIFY(r.frame.tail);
        QCOMPARE(r.frame.tailOffset, QPoint(-40, 175));
        s.frame.tail = false;
        QVERIFY(!TextSettings::fromJson(s.toJson()).frame.tail);
    }
};

QTEST_MAIN(TestText)
#include "tst_text.moc"
