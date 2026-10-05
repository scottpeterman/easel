#include "documentio.h"
#include "layerstack.h"
#include "shapes.h"
#include "tilestore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <climits>
#include <cstring>

using namespace easeletch;

namespace {

// The shape's image, with a helper to read it in canvas coordinates.
struct Drawn {
    ShapeLayout layout;
    QColor at(int x, int y) const
    {
        const QPoint p = QPoint(x, y) - layout.origin;
        if (!QRect(QPoint(0, 0), layout.image.size()).contains(p))
            return QColor(0, 0, 0, 0);
        return pixelToColor(reinterpret_cast<const Pixel *>(layout.image.constScanLine(p.y()))[p.x()]);
    }
};

Drawn draw(const ShapeSettings &s)
{
    return {layoutShape(s)};
}

ShapeSettings square()
{
    ShapeSettings s;
    s.points = {{100, 100}, {300, 100}, {300, 250}, {100, 250}};
    s.line = 6;
    s.lineColor = Qt::black;
    return s;
}

} // namespace

class TestShapes : public QObject
{
    Q_OBJECT

private slots:
    void aPolygonIsItsOutlineAndItsFill()
    {
        ShapeSettings s = square();
        Drawn d = draw(s);
        QVERIFY(!d.layout.image.isNull());
        QCOMPARE(d.layout.image.format(), TileStore::TileFormat);
        // The line is on the sides, centred on them; inside and outside are clear.
        QCOMPARE(d.at(200, 100), QColor(Qt::black));
        QCOMPARE(d.at(100, 180), QColor(Qt::black));
        QCOMPARE(d.at(300, 180), QColor(Qt::black));
        QCOMPARE(d.at(200, 250), QColor(Qt::black));
        QCOMPARE(d.at(200, 102), QColor(Qt::black));
        QCOMPARE(d.at(200, 180).alpha(), 0);
        QCOMPARE(d.at(200, 90).alpha(), 0);
        QCOMPARE(d.at(50, 180).alpha(), 0);
        // The corners are square, out to the line's own width.
        QCOMPARE(d.at(98, 98), QColor(Qt::black));

        s.filled = true;
        s.fill = QColor(200, 30, 30);
        d = draw(s);
        const QColor inside = d.at(200, 180);
        QVERIFY(qAbs(inside.red() - 200) <= 1 && qAbs(inside.green() - 30) <= 1);
        QCOMPARE(d.at(200, 100), QColor(Qt::black)); // the line is over the fill
        QCOMPARE(d.at(200, 90).alpha(), 0);

        // No line: only the fill, to the sides exactly.
        s.line = 0;
        d = draw(s);
        QCOMPARE(d.at(200, 101).red(), d.at(200, 180).red());
        QCOMPARE(d.at(200, 97).alpha(), 0);
        // Neither: a hairline, not nothing.
        s.filled = false;
        d = draw(s);
        QVERIFY(!d.layout.image.isNull());
        QVERIFY(d.at(200, 100).alpha() > 0 || d.at(200, 99).alpha() > 0);
        QCOMPARE(d.at(200, 180).alpha(), 0);
    }

    void openLinesCurvesAndHardEdges()
    {
        // Open: three sides, not four, and a fill has nothing to fill.
        ShapeSettings s = square();
        s.closed = false;
        s.filled = true;
        Drawn d = draw(s);
        QCOMPARE(d.at(200, 100), QColor(Qt::black));
        QCOMPARE(d.at(200, 250), QColor(Qt::black));
        QCOMPARE(d.at(100, 180).alpha(), 0); // no side from the last point back to the first
        QCOMPARE(d.at(200, 180).alpha(), 0);

        // Two points are a line; one is nothing.
        ShapeSettings line;
        line.points = {{10, 10}, {110, 10}};
        line.line = 4;
        d = draw(line);
        QCOMPARE(d.at(60, 10), QColor(Qt::black));
        line.points = {{10, 10}};
        QVERIFY(layoutShape(line).image.isNull());
        QVERIFY(!line.isDrawable());

        // Curved: through every point, and bulging out between them.
        s = square();
        s.curved = true;
        d = draw(s);
        QVERIFY(d.at(100, 100).alpha() > 200 || d.at(101, 101).alpha() > 200);
        QCOMPARE(d.at(200, 100).alpha(), 0); // the top side isn't straight any more ...
        bool above = false;
        for (int y = 60; y < 96 && !above; ++y)
            above = d.at(200, y).alpha() > 200; // ... it bows outwards
        QVERIFY(above);
        QVERIFY(s.path().contains(QPointF(200, 180)));

        // Smooth edges have in-between pixels on a slanted side; hard ones don't.
        ShapeSettings tri;
        tri.points = {{50, 50}, {250, 90}, {120, 230}};
        tri.line = 5;
        const auto partial = [](const QImage &img) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const float a = float(reinterpret_cast<const Pixel *>(img.constScanLine(y))[x].a);
                    n += a > 0.0f && a < 1.0f;
                }
            return n;
        };
        QVERIFY(partial(layoutShape(tri).image) > 100);
        tri.smooth = false;
        QCOMPARE(partial(layoutShape(tri).image), 0);
    }

    void hitTestsFindTheShapeItsPointsAndItsSides()
    {
        const ShapeSettings s = square();
        QVERIFY(shapeHit(s, {200, 180}, 4)); // inside
        QVERIFY(shapeHit(s, {200, 97}, 4));  // on the line
        QVERIFY(!shapeHit(s, {200, 80}, 4));
        ShapeSettings open = s;
        open.closed = false;
        QVERIFY(!shapeHit(open, {200, 180}, 4)); // an open line has no inside
        QVERIFY(shapeHit(open, {200, 101}, 4));

        QCOMPARE(shapePointAt(s, {302, 99}, 6), 1);
        QCOMPARE(shapePointAt(s, {200, 100}, 6), -1);
        QCOMPARE(shapeSideAt(s, {200, 102}, 6), 0);
        QCOMPARE(shapeSideAt(s, {298, 180}, 6), 1);
        QCOMPARE(shapeSideAt(s, {100, 180}, 6), 3); // the closing side
        QCOMPARE(shapeSideAt(open, {100, 180}, 6), -1);
        QCOMPARE(shapeSideAt(s, {200, 180}, 6), -1);
    }

    void aShapeLayerIsAShapeWhileItsPixelsAreItsOwn()
    {
        const QRect canvas(0, 0, 400, 300);
        Layer l;
        l.id = 2;
        l.name = QStringLiteral("Shape");
        l.hasShape = true;
        l.shape = square();
        l.shape.curved = true;
        l.shape.filled = true;
        l.shape.fill = QColor(10, 120, 200);
        l.shape.smooth = false;
        drawShapeLayer(l, canvas);
        QVERIFY(l.isShape());
        QVERIFY(!l.isText());
        QVERIFY(l.store.tileCount() > 0);
        const QImage first = l.store.tile(l.store.tileCoords().first());
        const TileCoord c = l.store.tileCoords().first();
        l.store.fillRect(QRect(0, 0, 4, 4), QColor(Qt::red));
        QVERIFY(!l.isShape());
        drawShapeLayer(l, canvas);
        QVERIFY(l.isShape());
        QVERIFY(std::memcmp(l.store.tile(c).constBits(), first.constBits(), size_t(TileStore::BytesPerTile)) == 0);

        // Moved by its points, it's drawn there; nothing is kept off the canvas.
        l.shape.translate(QPointF(250, 0));
        drawShapeLayer(l, canvas);
        QVERIFY(l.isShape());
        for (const TileCoord t : l.store.tileCoords())
            QVERIFY(TileStore::tileRect(t).intersects(canvas));
        l.shape.translate(QPointF(-250, 0));
        drawShapeLayer(l, canvas);

        // The points and the rest survive the file; a painted-on shape is saved as pixels.
        QTemporaryDir dir;
        LayerStack stack = LayerStack::single(TileStore(QColor(Qt::white)), canvas.size(), QStringLiteral("Background"));
        const int shape = stack.insert(l, 0, INT_MAX);
        Layer painted = l;
        painted.id = 0;
        painted.store.fillRect(QRect(0, 0, 10, 10), QColor(Qt::red));
        const int flat = stack.insert(painted, 0, INT_MAX);
        stack.recompositeAll();
        const QString path = dir.filePath(QStringLiteral("s.easeletch"));
        QCOMPARE(saveNativeDocument(path, stack), QString());
        const LoadedDocument doc = loadNativeDocument(path);
        QVERIFY2(doc.ok(), qPrintable(doc.error));
        const Layer *back = doc.stack->layer(shape);
        QVERIFY(back && back->isShape());
        QCOMPARE(back->shape.points, l.shape.points);
        QVERIFY(back->shape.curved && back->shape.closed && back->shape.filled && !back->shape.smooth);
        QCOMPARE(back->shape.line, 6);
        QCOMPARE(back->shape.fill, QColor(10, 120, 200));
        QVERIFY(doc.stack->layer(flat) && !doc.stack->layer(flat)->hasShape);

        // Nonsense in a file is brought into range, not trusted.
        QJsonObject bad = l.shape.toJson();
        bad.insert(QLatin1String("line"), 99999);
        QCOMPARE(ShapeSettings::fromJson(bad).line, ShapeSettings::MaxLine);
        bad.insert(QLatin1String("points"), QJsonArray{QJsonArray{1, 2}, QStringLiteral("x"), QJsonArray{3}});
        QCOMPARE(ShapeSettings::fromJson(bad).points.size(), 1);
    }
};

QTEST_MAIN(TestShapes)
#include "tst_shapes.moc"
