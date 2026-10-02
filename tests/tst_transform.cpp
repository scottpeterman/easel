#include "color.h"
#include "regionops.h"
#include "tilestore.h"
#include "transform.h"

#include <QTest>

using namespace easeletch;

namespace {

QImage blank(int w, int h)
{
    QImage img(w, h, TileStore::TileFormat);
    img.fill(Qt::transparent);
    return img;
}

void set(QImage &img, int x, int y, const QColor &c)
{
    reinterpret_cast<Pixel *>(img.scanLine(y))[x] = pixelFromColor(c);
}

Pixel at(const QImage &img, int x, int y)
{
    return reinterpret_cast<const Pixel *>(img.constScanLine(y))[x];
}

QColor colorAt(const QImage &img, int x, int y)
{
    return pixelToColor(at(img, x, y));
}

// 3 x 2, every pixel a different colour.
QImage sixColours()
{
    QImage img = blank(3, 2);
    const QColor c[6] = {Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta};
    for (int i = 0; i < 6; ++i)
        set(img, i % 3, i / 3, c[i]);
    return img;
}

FreeTransform boxFor(const QImage &img, const QPoint &topLeft)
{
    FreeTransform t;
    t.size = img.size();
    t.center = QPointF(topLeft) + QPointF(img.width() / 2.0, img.height() / 2.0);
    return t;
}

double alphaSum(const QImage &img)
{
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            sum += double(float(at(img, x, y).a));
    return sum;
}

} // namespace

class TestTransform : public QObject
{
    Q_OBJECT

private slots:
    void identityIsUntouched()
    {
        const QImage src = sixColours();
        FreeTransform t = boxFor(src, {10, 20});
        QVERIFY(t.isIdentity());
        QPoint origin;
        const QImage out = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(origin, QPoint(10, 20));
        QCOMPARE(out.size(), src.size());
        for (int i = 0; i < 6; ++i)
            QVERIFY(samePixel(at(out, i % 3, i / 3), at(src, i % 3, i / 3)));
    }

    void flipsAreExact()
    {
        const QImage src = sixColours();
        for (const bool smooth : {true, false}) {
            FreeTransform t = boxFor(src, {10, 20});
            t.flipHorizontal();
            QVERIFY(!t.isIdentity());
            QPoint origin;
            QImage out = transformImage(src, t.matrix(), smooth, &origin);
            QCOMPARE(origin, QPoint(10, 20));
            QCOMPARE(out.size(), QSize(3, 2));
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 3; ++x)
                    QVERIFY(samePixel(at(out, x, y), at(src, 2 - x, y)));

            t = boxFor(src, {10, 20});
            t.flipVertical();
            out = transformImage(src, t.matrix(), smooth, &origin);
            QCOMPARE(origin, QPoint(10, 20));
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 3; ++x)
                    QVERIFY(samePixel(at(out, x, y), at(src, x, 1 - y)));
        }
    }

    void quarterTurnsAreExactAndOnTheGrid()
    {
        // 3 x 2 turned about its centre would sit on half pixels; it's kept on whole ones.
        const QImage src = sixColours();
        FreeTransform t = boxFor(src, {10, 20});
        t.quarterRight();
        QPoint origin;
        QImage out = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(out.size(), QSize(2, 3));
        // Clockwise: the source's left column becomes the top row, read right to left.
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                QVERIFY(samePixel(at(out, 1 - y, x), at(src, x, y)));

        t.quarterLeft();
        QVERIFY(std::abs(t.angle) < 1e-9);
        t.halfTurn();
        out = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(origin, QPoint(10, 20));
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                QVERIFY(samePixel(at(out, 2 - x, 1 - y), at(src, x, y)));
    }

    void flipOfARotatedBoxMirrorsTheCanvas()
    {
        // Flip Horizontal means left-to-right as seen, whatever the rotation.
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        t.rotate(30);
        const QPointF before = t.matrix().map(QPointF(0, 0));
        t.flipHorizontal();
        const QPointF after = t.matrix().map(QPointF(0, 0));
        QVERIFY(std::abs(after.x() - (200.0 - before.x())) < 1e-6);
        QVERIFY(std::abs(after.y() - before.y()) < 1e-6);
    }

    void hardPixelsScaleUpInBlocks()
    {
        QImage src = blank(2, 1);
        set(src, 0, 0, Qt::red);
        set(src, 1, 0, Qt::blue);
        FreeTransform t = boxFor(src, {0, 0});
        t.scaleX = t.scaleY = 3.0;
        QPoint origin;
        const QImage out = transformImage(src, t.matrix(), false, &origin);
        QCOMPARE(out.size(), QSize(6, 3));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 6; ++x)
                QVERIFY(samePixel(at(out, x, y), at(src, x / 3, 0)));
    }

    void smoothScaleUpKeepsEdgesSolid()
    {
        // A solid block scaled up is solid to its edge: it doesn't fade into
        // the transparency around it.
        QImage src = blank(4, 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                set(src, x, y, Qt::red);
        FreeTransform t = boxFor(src, {8, 8});
        t.scaleX = t.scaleY = 2.5;
        QPoint origin;
        const QImage out = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(out.size(), QSize(10, 10));
        QCOMPARE(origin, QPoint(5, 5));
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                QCOMPARE(colorAt(out, x, y), QColor(Qt::red));
    }

    void smoothScaleDownAverages()
    {
        // Black and white 1 px checks at half size: every pixel is their average.
        QImage src = blank(8, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                set(src, x, y, (x + y) % 2 ? Qt::white : Qt::black);
        FreeTransform t = boxFor(src, {0, 0});
        t.scaleX = t.scaleY = 0.5;
        QPoint origin;
        const QImage out = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(out.size(), QSize(4, 4));
        QCOMPARE(origin, QPoint(2, 2));
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const Pixel p = at(out, x, y);
                QVERIFY2(std::abs(float(p.r) - 0.5f) < 0.01f, qPrintable(QString::number(float(p.r))));
                QVERIFY(std::abs(float(p.a) - 1.0f) < 0.01f);
            }
        }
        // A quarter size skips nothing either.
        t.scaleX = t.scaleY = 0.25;
        const QImage quarter = transformImage(src, t.matrix(), true, &origin);
        QCOMPARE(quarter.size(), QSize(2, 2));
        QVERIFY(std::abs(float(at(quarter, 0, 0).r) - 0.5f) < 0.01f);
    }

    void rotationKeepsTheAreaAndSoftensTheEdge()
    {
        QImage src = blank(40, 40);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x)
                set(src, x, y, Qt::red);
        FreeTransform t = boxFor(src, {100, 100});
        t.rotate(30);
        QPoint origin;
        const QImage smooth = transformImage(src, t.matrix(), true, &origin);
        QVERIFY2(std::abs(alphaSum(smooth) - 1600.0) < 16.0, qPrintable(QString::number(alphaSum(smooth))));
        QCOMPARE(colorAt(smooth, smooth.width() / 2, smooth.height() / 2), QColor(Qt::red));
        int partial = 0;
        for (int y = 0; y < smooth.height(); ++y)
            for (int x = 0; x < smooth.width(); ++x)
                partial += float(at(smooth, x, y).a) > 0.02f && float(at(smooth, x, y).a) < 0.98f;
        QVERIFY(partial > 40); // antialiased along all four edges

        // Hard pixels: every pixel is all or nothing.
        const QImage hard = transformImage(src, t.matrix(), false, &origin);
        QVERIFY(std::abs(alphaSum(hard) - 1600.0) < 40.0);
        for (int y = 0; y < hard.height(); ++y)
            for (int x = 0; x < hard.width(); ++x)
                QVERIFY(float(at(hard, x, y).a) == 0.0f || float(at(hard, x, y).a) == 1.0f);
    }

    void clipBoundsTheResult()
    {
        const QImage src = sixColours();
        FreeTransform t = boxFor(src, {0, 0});
        t.scaleX = t.scaleY = 5000.0;
        t.center = QPointF(50, 50);
        QPoint origin;
        const QImage out = transformImage(src, t.matrix(), true, &origin, QRect(0, 0, 100, 80));
        QCOMPARE(out.size(), QSize(100, 80));
        QCOMPARE(origin, QPoint(0, 0));
        // Entirely off the canvas: a token pixel, not an error.
        t.scaleX = t.scaleY = 1.0;
        t.center = QPointF(-500, -500);
        const QImage off = transformImage(src, t.matrix(), true, &origin, QRect(0, 0, 100, 80));
        QCOMPARE(off.size(), QSize(1, 1));
    }

    void cornerDragScalesInProportionAboutTheOppositeCorner()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        const QPointF anchor = handlePosition(t, TransformHandle::TopLeft);
        QCOMPARE(anchor, QPointF(80, 90));
        // Dragged 40 right and 5 down: the width decides, the height follows.
        FreeTransform r = dragHandle(t, TransformHandle::BottomRight, {120, 110}, {160, 115}, false);
        QCOMPARE(r.scaleX, 2.0);
        QCOMPARE(r.scaleY, 2.0);
        QCOMPARE(handlePosition(r, TransformHandle::TopLeft), anchor);
        QCOMPARE(handlePosition(r, TransformHandle::BottomRight), QPointF(160, 130));

        // Shift: any shape.
        r = dragHandle(t, TransformHandle::BottomRight, {120, 110}, {160, 115}, true);
        QCOMPARE(r.scaleX, 2.0);
        QCOMPARE(r.scaleY, 1.25);
        QCOMPARE(handlePosition(r, TransformHandle::TopLeft), anchor);
    }

    void sideDragStretchesOneAxis()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        const FreeTransform r = dragHandle(t, TransformHandle::Right, {120, 100}, {130, 140}, false);
        QCOMPARE(r.scaleX, 1.25);
        QCOMPARE(r.scaleY, 1.0);
        QCOMPARE(handlePosition(r, TransformHandle::Left), QPointF(80, 100));

        const FreeTransform up = dragHandle(t, TransformHandle::Top, {100, 90}, {100, 70}, false);
        QCOMPARE(up.scaleY, 2.0);
        QCOMPARE(handlePosition(up, TransformHandle::Bottom), QPointF(100, 110));
    }

    void draggingPastTheAnchorFlips()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        const FreeTransform r = dragHandle(t, TransformHandle::Right, {120, 100}, {60, 100}, false);
        QCOMPARE(r.scaleX, -0.5);
        // Never zero: there's always a pixel left.
        const FreeTransform z = dragHandle(t, TransformHandle::Right, {120, 100}, {80, 100}, false);
        QVERIFY(z.scaleX != 0.0);
    }

    void sizesAreWholePixelsUntilRotated()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        FreeTransform r = dragHandle(t, TransformHandle::Right, {120, 100}, {127.4, 100}, false);
        QCOMPARE(r.size.width() * r.scaleX, 47.0);
        t.rotate(30);
        r = dragHandle(t, TransformHandle::Right, handlePosition(t, TransformHandle::Right),
                       handlePosition(t, TransformHandle::Right) + QPointF(3.3, 1.9), false);
        QVERIFY(std::abs(r.size.width() * r.scaleX - std::round(r.size.width() * r.scaleX)) > 1e-3);
        // The opposite side hasn't moved.
        const QPointF a = handlePosition(t, TransformHandle::Left), b = handlePosition(r, TransformHandle::Left);
        QVERIFY(std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9);
    }

    void rotateDragTurnsAboutTheCentre()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        FreeTransform r = dragHandle(t, TransformHandle::Rotate, {160, 100}, {100, 160}, false);
        QVERIFY(std::abs(r.angle - 90.0) < 1e-9); // clockwise on screen
        QCOMPARE(r.center, t.center);
        r = dragHandle(t, TransformHandle::Rotate, {160, 100}, {160, 120}, true);
        QVERIFY(std::abs(r.angle - 15.0) < 1e-9); // Shift: 15 degree steps
    }

    void moveDragKeepsWholePixels()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        FreeTransform r = dragHandle(t, TransformHandle::Move, {100, 100}, {110.4, 96.6}, false);
        QCOMPARE(r.center, QPointF(110, 97));
        r = dragHandle(t, TransformHandle::Move, {100, 100}, {110.4, 96.6}, true);
        QCOMPARE(r.center, QPointF(110, 100)); // Shift: one axis
    }

    void hitTesting()
    {
        FreeTransform t;
        t.size = QSizeF(100, 60);
        t.center = QPointF(200, 200);
        QCOMPARE(hitTest(t, {150, 170}, 6), TransformHandle::TopLeft);
        QCOMPARE(hitTest(t, {253, 232}, 6), TransformHandle::BottomRight);
        QCOMPARE(hitTest(t, {200, 172}, 6), TransformHandle::Top);
        QCOMPARE(hitTest(t, {248, 200}, 6), TransformHandle::Right);
        QCOMPARE(hitTest(t, {200, 200}, 6), TransformHandle::Move);
        QCOMPARE(hitTest(t, {300, 200}, 6), TransformHandle::Rotate);
        // Rotated a quarter turn, the handles go with the box.
        t.rotate(90);
        QCOMPARE(hitTest(t, {230, 150}, 6), TransformHandle::TopLeft);
        // A small box: its middle moves it, its corners still scale it, and
        // it has no side handles.
        FreeTransform s;
        s.size = QSizeF(20, 20);
        s.center = QPointF(50, 50);
        QCOMPARE(hitTest(s, {52, 47}, 6), TransformHandle::Move);
        QCOMPARE(hitTest(s, {60, 60}, 6), TransformHandle::BottomRight);
        QCOMPARE(hitTest(s, {58, 57}, 6), TransformHandle::BottomRight);
        QCOMPARE(hitTest(s, {60, 50}, 6), TransformHandle::Move);
        QCOMPARE(hitTest(s, {70, 50}, 6), TransformHandle::Rotate);
    }

    void shapesFollowTheTransform()
    {
        FreeTransform t;
        t.size = QSizeF(40, 20);
        t.center = QPointF(100, 100);
        // The whole block stays a plain rectangle through flips, turns and scaling.
        t.quarterRight();
        Selection s = transformShape(Selection::rect(QRect(0, 0, 40, 20)), QSize(40, 20), t.matrix());
        QCOMPARE(s.shape(), Selection::Shape::Rect);
        QCOMPARE(s.bounds(), QRect(0, 0, 20, 40));
        // An ellipse keeps its corners empty.
        s = transformShape(Selection::ellipse(QRect(0, 0, 40, 20)), QSize(40, 20), t.matrix());
        QCOMPARE(s.bounds(), QRect(0, 0, 20, 40));
        QVERIFY(!s.contains(0, 0));
        QVERIFY(s.contains(10, 20));
        // Rotated off the grid, the rectangle's outline is the tilted box.
        t.rotate(-60);
        s = transformShape(Selection::rect(QRect(0, 0, 40, 20)), QSize(40, 20), t.matrix());
        QCOMPARE(s.shape(), Selection::Shape::Mask);
        QVERIFY(!s.contains(0, 0));
        QVERIFY(s.contains(s.bounds().width() / 2, s.bounds().height() / 2));
    }

    void floatingContentCanBeReplaced()
    {
        TileStore store(Qt::white);
        const QRect canvas(0, 0, 200, 200);
        store.fillRect(QRect(50, 50, 10, 10), QColor(Qt::red));
        const TileStore original = store.snapshot();

        FloatingContent f;
        f.lift(&store, Selection::rect(QRect(50, 50, 10, 10)), canvas);
        QImage big = blank(20, 20);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                set(big, x, y, Qt::blue);
        f.replace(big, {}, QPoint(100, 100));
        QCOMPARE(f.position(), QPoint(100, 100));
        QCOMPARE(f.selection().bounds(), QRect(100, 100, 20, 20));
        QCOMPARE(store.defaultColor(), QColor(Qt::white));
        QCOMPARE(pixelToColor(store.pixel(110, 110)), QColor(Qt::blue));
        QCOMPARE(pixelToColor(store.pixel(55, 55)).alpha(), 0); // the hole it was lifted from

        // Replaced again somewhere else: the first place goes back to what was under it.
        f.replace(big, {}, QPoint(10, 10));
        QCOMPARE(pixelToColor(store.pixel(110, 110)), QColor(Qt::white));
        QCOMPARE(pixelToColor(store.pixel(15, 15)), QColor(Qt::blue));

        f.cancel();
        for (const TileCoord c : TileStore::tilesIntersecting(canvas))
            QVERIFY(store.tile(c) == original.tile(c));
    }
};

QTEST_GUILESS_MAIN(TestTransform)
#include "tst_transform.moc"
