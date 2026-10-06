#include "documentio.h"
#include "layerstack.h"
#include "shapes.h"
#include "tilestore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <climits>
#include <cmath>
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

    void aGradientFillsTheShapeAndStaysInside()
    {
        ShapeSettings s = square(); // 100..300 across, 100..250 down
        s.filled = true;
        s.fill = Qt::green; // not used: the gradient takes its place
        s.gradientFill = true;
        s.gradientStops = twoStops(Qt::white, Qt::black);
        Drawn d = draw(s);
        // Top to bottom, unless told otherwise: light at the top, dark at the
        // bottom, the same all the way across.
        const int top = d.at(200, 110).red(), middle = d.at(200, 175).red(), bottom = d.at(200, 240).red();
        QVERIFY(top > 220);
        QVERIFY(bottom < 60);
        QVERIFY(middle < top && middle > bottom);
        QCOMPARE(d.at(120, 175).red(), d.at(280, 175).red());
        QCOMPARE(d.at(200, 175).green(), d.at(200, 175).red()); // grey, not the flat fill's green
        QCOMPARE(d.at(200, 175).alpha(), 255);
        // The outline is over it, and nothing is outside the shape.
        QCOMPARE(d.at(200, 100), QColor(Qt::black));
        QCOMPARE(d.at(100, 110), QColor(Qt::black));
        QCOMPARE(d.at(200, 90).alpha(), 0);
        QCOMPARE(d.at(320, 175).alpha(), 0);
        const Gradient worked = s.gradient();
        QCOMPARE(worked.from, QPointF(200, 100));
        QCOMPARE(worked.to, QPointF(200, 250));

        // Its line placed by hand: left to right, over part of the shape only.
        s.gradientPlaced = true;
        s.gradientFrom = QPointF(150, 175);
        s.gradientTo = QPointF(250, 175);
        d = draw(s);
        QVERIFY(d.at(120, 175).red() > 250); // before the start: the first colour
        QVERIFY(d.at(280, 175).red() < 5);   // past the end: the last
        QVERIFY(d.at(200, 120).red() > 60 && d.at(200, 120).red() < 230);
        QCOMPARE(d.at(200, 120).red(), d.at(200, 230).red());
        // Moved, the line moves with the points.
        s.translate(QPointF(40, 0));
        QCOMPARE(s.gradientFrom, QPointF(190, 175));
        QCOMPARE(draw(s).at(160, 175).red(), d.at(120, 175).red());
        s.translate(QPointF(-40, 0));

        // A transparent end leaves the shape see-through there; the other
        // spreads start from the middle.
        s.gradientPlaced = false;
        s.gradientStops = twoStops(Qt::red, Qt::transparent);
        d = draw(s);
        QVERIFY(d.at(200, 110).alpha() > 220);
        QVERIFY(d.at(200, 240).alpha() < 40);
        s.gradientStops = twoStops(Qt::white, Qt::black);
        s.gradientShape = GradientShape::Radial;
        d = draw(s);
        QVERIFY(d.at(200, 175).red() > 240);
        QVERIFY(d.at(110, 110).red() < d.at(160, 150).red());
        QCOMPARE(s.gradient().from, QPointF(200, 175));
        s.gradientShape = GradientShape::Reflected;
        d = draw(s);
        QVERIFY(qAbs(d.at(200, 120).red() - d.at(200, 229).red()) <= 3); // the same above and below the middle

        // Hard edges: the colours still run, but every pixel is whole.
        s.gradientShape = GradientShape::Linear;
        s.smooth = false;
        const QImage hard = layoutShape(s).image;
        for (int y = 0; y < hard.height(); ++y)
            for (int x = 0; x < hard.width(); ++x) {
                const float a = float(reinterpret_cast<const Pixel *>(hard.constScanLine(y))[x].a);
                QVERIFY(a == 0.0f || a == 1.0f);
            }
        s.smooth = true;

        // An open line has nothing to fill, gradient or not; and a gradient
        // without Fill is no fill.
        ShapeSettings open = s;
        open.closed = false;
        QCOMPARE(draw(open).at(200, 175).alpha(), 0);
        ShapeSettings unfilled = s;
        unfilled.filled = false;
        QVERIFY(!unfilled.hasGradient());
        QCOMPARE(draw(unfilled).at(200, 175).alpha(), 0);

        // It survives the file, placed line and all; one never placed stays unplaced.
        s.gradientShape = GradientShape::Conical;
        s.gradientPlaced = true;
        s.gradientFrom = QPointF(150.5, 175);
        s.gradientTo = QPointF(250, 180.25);
        s.gradientStops = {{0.0, QColor(Qt::red)}, {0.4, QColor(Qt::yellow)}, {1.0, QColor(0, 0, 255, 128)}};
        ShapeSettings r = ShapeSettings::fromJson(s.toJson());
        QVERIFY(r.hasGradient() && r.gradientPlaced);
        QCOMPARE(r.gradientShape, GradientShape::Conical);
        QCOMPARE(r.gradientFrom, s.gradientFrom);
        QCOMPARE(r.gradientTo, s.gradientTo);
        QCOMPARE(r.gradientStops, normalizedStops(s.gradientStops));
        s.gradientPlaced = false;
        r = ShapeSettings::fromJson(s.toJson());
        QVERIFY(r.hasGradient() && !r.gradientPlaced);
        s.gradientFill = false;
        QVERIFY(!s.toJson().contains(QLatin1String("gradient")));
        QVERIFY(!ShapeSettings::fromJson(s.toJson()).hasGradient());

        // As a layer it's still a shape, drawn the same every time.
        Layer l;
        l.id = 3;
        l.hasShape = true;
        l.shape = s;
        l.shape.gradientFill = true;
        drawShapeLayer(l, QRect(0, 0, 400, 300));
        QVERIFY(l.isShape());
        const TileStore first = l.store.snapshot();
        drawShapeLayer(l, QRect(0, 0, 400, 300));
        for (const TileCoord c : first.tileCoords())
            QVERIFY(std::memcmp(first.tile(c).constBits(), l.store.tile(c).constBits(),
                                size_t(TileStore::BytesPerTile)) == 0);
    }

    void anOpenLineTapersToAPointAtItsEnds()
    {
        ShapeSettings s;
        s.points = {{50, 100}, {350, 100}};
        s.closed = false;
        s.line = 20;
        s.taperStart = 50;
        // Half the line fills out from nothing; the rest is the full width.
        QCOMPARE(s.lineWidthAt(0.0), 0.0);
        QVERIFY(qAbs(s.lineWidthAt(0.25) - 20.0 * std::sqrt(0.5)) < 1e-9);
        QCOMPARE(s.lineWidthAt(0.5), 20.0);
        QCOMPARE(s.lineWidthAt(1.0), 20.0);
        Drawn d = draw(s);
        QCOMPARE(d.at(340, 100), QColor(Qt::black));
        QCOMPARE(d.at(340, 107), QColor(Qt::black));
        QCOMPARE(d.at(200, 107), QColor(Qt::black));
        QVERIFY(d.at(60, 100).alpha() > 100); // thin here, but there
        QCOMPARE(d.at(60, 107).alpha(), 0);
        QCOMPARE(d.at(125, 109).alpha(), 0); // 14 px wide a quarter of the way along
        QCOMPARE(d.at(125, 104), QColor(Qt::black));
        // The far end is round and whole, as an untapered line's is.
        QCOMPARE(d.at(355, 100), QColor(Qt::black));
        QCOMPARE(d.at(365, 100).alpha(), 0);

        // Both ends, by more than there's line for: they meet where their shares put them.
        s.taperStart = 100;
        s.taperEnd = 100;
        QCOMPARE(s.lineWidthAt(0.5), 20.0);
        QVERIFY(s.lineWidthAt(0.4) < 20.0 && s.lineWidthAt(0.6) < 20.0);
        d = draw(s);
        QCOMPARE(d.at(200, 107), QColor(Qt::black));
        QCOMPARE(d.at(60, 107).alpha(), 0);
        QCOMPARE(d.at(340, 107).alpha(), 0);

        // Hard pixels: no in-between ones, taper or not.
        s.smooth = false;
        d = draw(s);
        for (int y = 85; y <= 115; ++y)
            for (int x = 40; x <= 360; ++x) {
                const int a = d.at(x, y).alpha();
                QVERIFY2(a == 0 || a == 255, qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(a)));
            }

        // A shape that joins up with itself has no ends: it's drawn as it always was.
        ShapeSettings sq = square();
        const QImage plain = draw(sq).layout.image;
        sq.taperStart = 60;
        sq.taperEnd = 60;
        QVERIFY(!sq.isTapered());
        QCOMPARE(sq.lineWidthAt(0.0), 6.0);
        QCOMPARE(draw(sq).layout.image, plain);

        // It survives the file; a shape without one doesn't mention it.
        s.taperStart = 35;
        s.taperEnd = 80;
        const ShapeSettings r = ShapeSettings::fromJson(s.toJson());
        QCOMPARE(r.taperStart, 35);
        QCOMPARE(r.taperEnd, 80);
        QVERIFY(!square().toJson().contains(QLatin1String("taper")));
        QCOMPARE(ShapeSettings::fromJson(square().toJson()).taperStart, 0);
        QJsonObject wild = s.toJson();
        wild.insert(QLatin1String("taper"), QJsonArray{-20, 900});
        QCOMPARE(ShapeSettings::fromJson(wild).taperStart, 0);
        QCOMPARE(ShapeSettings::fromJson(wild).taperEnd, 100);
    }

    void aCurveCanTurnASharpCornerOrBeAimedByHand()
    {
        ShapeSettings s;
        s.points = {{50, 200}, {200, 50}, {350, 200}};
        s.closed = false;
        s.curved = true;
        s.line = 6;
        // Smooth: the line leaves the middle point the way its neighbours lie.
        QCOMPARE(s.handleOut(1), QPointF(50, 0));
        QPainterPath path = s.path();
        QCOMPARE(path.elementCount(), 7); // a start and two cubics
        QCOMPARE(QPointF(path.elementAt(2)), QPointF(150, 50));
        QCOMPARE(QPointF(path.elementAt(3)), QPointF(200, 50));
        QCOMPARE(QPointF(path.elementAt(4)), QPointF(250, 50));
        QCOMPARE(draw(s).at(200, 50), QColor(Qt::black));

        // A corner: the curve comes straight in and goes straight out.
        s.setNode(1, {ShapeNode::Corner, {}});
        QCOMPARE(s.handleOut(1), QPointF());
        path = s.path();
        QCOMPARE(QPointF(path.elementAt(2)), QPointF(200, 50));
        QCOMPARE(QPointF(path.elementAt(4)), QPointF(200, 50));
        // It no longer swings wide of the point on its way through.
        QCOMPARE(draw(s).at(225, 50).alpha(), 0);
        QCOMPARE(draw(s).at(200, 51), QColor(Qt::black));

        // Aimed by hand: out the way the handle points, in from the opposite side.
        s.setNode(1, {ShapeNode::Handle, {0, 90}});
        QCOMPARE(s.handleOut(1), QPointF(0, 90));
        path = s.path();
        QCOMPARE(QPointF(path.elementAt(2)), QPointF(200, -40));
        QCOMPARE(QPointF(path.elementAt(4)), QPointF(200, 140));
        // The other points are still worked out, and every point is still on the line.
        QCOMPARE(s.handleOut(0), QPointF(25, -25));
        QCOMPARE(QPointF(path.elementAt(3)), QPointF(200, 50));
        QCOMPARE(QPointF(path.elementAt(6)), QPointF(350, 200));

        // Points added and removed take their nodes with them.
        s.insertPoint(1, {120, 120});
        QCOMPARE(s.points.size(), 4);
        QCOMPARE(s.node(1).kind, ShapeNode::Smooth);
        QCOMPARE(s.node(2).kind, ShapeNode::Handle);
        s.removePoint(0);
        QCOMPARE(s.node(1).kind, ShapeNode::Handle);
        QCOMPARE(s.node(1).out, QPointF(0, 90));
        s.insertPoint(int(s.points.size()), {380, 260});
        QCOMPARE(s.node(3).kind, ShapeNode::Smooth);
        QCOMPARE(s.node(99).kind, ShapeNode::Smooth);

        // The file keeps them, in step with the points; a plain curve doesn't mention them.
        s.setNode(0, {ShapeNode::Corner, {}});
        const ShapeSettings r = ShapeSettings::fromJson(s.toJson());
        QCOMPARE(r.points, s.points);
        QCOMPARE(r.node(0).kind, ShapeNode::Corner);
        QCOMPARE(r.node(1).kind, ShapeNode::Handle);
        QCOMPARE(r.node(1).out, QPointF(0, 90));
        QCOMPARE(r.node(2).kind, ShapeNode::Smooth);
        QCOMPARE(r.path(), s.path());
        ShapeSettings plain = s;
        plain.nodes.clear();
        QVERIFY(!plain.toJson().contains(QLatin1String("nodes")));

        // Two points make a straight line, until one is aimed: then an arc.
        ShapeSettings arc;
        arc.points = {{50, 100}, {250, 100}};
        arc.closed = false;
        arc.curved = true;
        arc.line = 6;
        QCOMPARE(draw(arc).at(137, 100), QColor(Qt::black));
        arc.setNode(0, {ShapeNode::Handle, {0, -80}});
        const Drawn d = draw(arc);
        QCOMPARE(d.at(137, 100).alpha(), 0);
        QCOMPARE(d.at(137, 70), QColor(Qt::black));

        // A straight-sided shape takes no notice of any of it.
        ShapeSettings straight = s;
        straight.curved = false;
        ShapeSettings bare = straight;
        bare.nodes.clear();
        QCOMPARE(straight.path(), bare.path());
    }
};

QTEST_MAIN(TestShapes)
#include "tst_shapes.moc"
