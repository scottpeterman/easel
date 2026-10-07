#include "brushpresets.h"
#include "brushtool.h"
#include "canvasview.h"
#include "mainwindow.h"

#include <QSettings>
#include <QTest>

using namespace easeletch;

// Sketch first, colour after: a sketch layer on top that lets the colour
// under it through.

namespace {

void setupWindow(MainWindow &w)
{
    w.resize(1221, 718);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
}

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

void stroke(MainWindow &w, QPointF from, QPointF to)
{
    CanvasView *view = w.canvasView();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, from));
    for (int i = 1; i <= 20; ++i)
        QTest::mouseMove(view, viewPos(w, from + (to - from) * (i / 20.0)));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, to));
}

QColor shown(MainWindow &w, int x, int y)
{
    w.canvasView()->refresh();
    return pixelToColor(w.layers().composite().pixel(x, y));
}

bool near(const QColor &a, const QColor &b, int tol = 3)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

QStringList names(const MainWindow &w)
{
    QStringList out; // bottom to top
    for (int id : w.layers().children(0))
        out << w.layers().layer(id)->name;
    return out;
}

// A solid, hard line however the brush was left.
void inkPen(MainWindow &w, const QColor &color, double size)
{
    QVERIFY(w.chooseBrushPreset(QStringLiteral("ink-tech")));
    BrushSettings b = w.brushTool()->settings();
    b.size = size;
    b.hardness = 1.0;
    b.stabilizer = 0.0;
    w.brushTool()->setSettings(b);
    w.brushTool()->setColor(color);
}

} // namespace

class TestSketch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("EaseletchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_sketch"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void aNewSketchLayerGoesOnTopWithAPencil()
    {
        MainWindow w;
        setupWindow(w);
        const int background = w.layers().activeId();
        QVERIFY(w.addLayer());
        QVERIFY(w.addLayer());
        w.setActiveLayer(background); // wherever the active layer is, the sketch goes over everything
        QVERIFY(w.chooseBrushPreset(QStringLiteral("flat")));

        QVERIFY(w.addSketchLayer());
        const Layer *sketch = w.layers().active();
        QVERIFY(sketch);
        QCOMPARE(sketch->name, QStringLiteral("Sketch"));
        QCOMPARE(sketch->blend, BlendMode::Multiply);
        QCOMPARE(w.layers().children(0).last(), sketch->id);
        QCOMPARE(w.layers().children(0).size(), 4);
        // A paint brush was swapped for a pencil, and the brush is the tool.
        QCOMPARE(w.brushTool()->preset(), QStringLiteral("pencil-2b"));
        QCOMPARE(w.brushTool()->mode(), BrushMode::Paint);
        QCOMPARE(w.canvasView()->tool(), static_cast<CanvasTool *>(w.brushTool()));
        QCOMPARE(w.history().undoLabel(), QStringLiteral("New Sketch Layer"));

        // Charcoal already in hand stays in hand.
        QVERIFY(w.chooseBrushPreset(QStringLiteral("charcoal")));
        QVERIFY(w.addSketchLayer());
        QCOMPARE(w.brushTool()->preset(), QStringLiteral("charcoal"));
        QCOMPARE(w.layers().active()->name, QStringLiteral("Sketch 2"));

        w.undo();
        w.undo();
        QCOMPARE(w.layers().children(0).size(), 3);
    }

    void colourPaintedUnderTheSketchShowsBetweenItsLines()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addSketchLayer());
        const int sketch = w.layers().activeId();
        inkPen(w, Qt::black, 6);
        stroke(w, {60, 150}, {340, 150}); // a line of the sketch
        QVERIFY(near(shown(w, 200, 150), Qt::black));

        QVERIFY(w.addLayerBelow());
        const Layer *color = w.layers().active();
        QVERIFY(color->id != sketch);
        QCOMPARE(w.layers().children(0).indexOf(color->id), w.layers().children(0).indexOf(sketch) - 1);
        QCOMPARE(color->blend, BlendMode::Normal);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("New Layer Below"));

        // A broad band of colour straight across the line.
        inkPen(w, QColor(0, 160, 255), 60);
        stroke(w, {200, 60}, {200, 240});
        QVERIFY(near(shown(w, 200, 100), QColor(0, 160, 255))); // colour, beside the line
        QVERIFY(near(shown(w, 200, 150), Qt::black));           // the line, still on top of it
        QVERIFY(near(shown(w, 100, 150), Qt::black));
        QVERIFY(near(shown(w, 100, 100), Qt::white));
    }

    void aSketchOnTheBackgroundIsLiftedOverNewColour()
    {
        MainWindow w;
        setupWindow(w);
        const int background = w.layers().activeId();
        inkPen(w, Qt::black, 6);
        stroke(w, {60, 150}, {340, 150}); // drawn straight onto the white background
        QCOMPARE(names(w), QStringList{QStringLiteral("Background")});

        QVERIFY(w.makeSketchFromLayer());
        QCOMPARE(names(w), (QStringList{QStringLiteral("Paper"), QStringLiteral("Color"), QStringLiteral("Sketch")}));
        const Layer *sketch = w.layers().layer(background); // the same layer, renamed and moved
        QVERIFY(sketch);
        QCOMPARE(sketch->name, QStringLiteral("Sketch"));
        QCOMPARE(sketch->blend, BlendMode::Multiply);
        QCOMPARE(w.layers().active()->name, QStringLiteral("Color"));
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Sketch from Layer"));
        // Nothing looks different yet.
        QVERIFY(near(shown(w, 200, 150), Qt::black));
        QVERIFY(near(shown(w, 200, 100), Qt::white));

        // Colour goes on under the lines, through the sketch's white paper.
        inkPen(w, QColor(255, 120, 0), 60);
        stroke(w, {200, 60}, {200, 240});
        QVERIFY(near(shown(w, 200, 100), QColor(255, 120, 0)));
        QVERIFY(near(shown(w, 200, 150), Qt::black));
        QVERIFY(near(shown(w, 60, 60), Qt::white));
        // The sketch itself wasn't painted on.
        QVERIFY(near(pixelToColor(sketch->store.pixel(200, 100)), Qt::white));

        // One step back for the stroke, one for the whole rearrangement.
        w.undo();
        w.undo();
        QCOMPARE(names(w), QStringList{QStringLiteral("Background")});
        QCOMPARE(w.layers().layer(background)->blend, BlendMode::Normal);
        QVERIFY(near(shown(w, 200, 150), Qt::black));
    }

    void aSketchOnAnUpperLayerNeedsNoNewPaper()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addLayer());
        const int drawn = w.layers().activeId();
        QVERIFY(w.addLayer()); // something above it
        w.setActiveLayer(drawn);

        QVERIFY(w.makeSketchFromLayer());
        const QStringList now = names(w);
        QCOMPARE(now.size(), 4);
        QCOMPARE(now.first(), QStringLiteral("Background")); // still the paper
        QCOMPARE(now.last(), QStringLiteral("Sketch"));
        QCOMPARE(now.at(2), QStringLiteral("Color"));
        QVERIFY(!now.contains(QStringLiteral("Paper")));
        QCOMPARE(w.layers().children(0).last(), drawn);
    }

    void aGroupCannotBeASketch()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addLayer());
        QVERIFY(w.addGroup());
        const int layer = w.layers().activeId();
        w.setActiveLayer(w.layers().layer(layer)->parent);
        QVERIFY(w.layers().active()->group);
        const QStringList before = names(w);
        QVERIFY(!w.makeSketchFromLayer());
        QCOMPARE(names(w), before);
    }
};

QTEST_MAIN(TestSketch)
#include "tst_sketch.moc"
