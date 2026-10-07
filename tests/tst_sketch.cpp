#include "brushpresets.h"
#include "brushtool.h"
#include "canvasview.h"
#include "mainwindow.h"
#include "newdocumentdialog.h"
#include "paper.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
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

    // --- Paper ----------------------------------------------------------------

    void aPaperDocumentIsALockedSheetWithALayerToDrawOn()
    {
        MainWindow w;
        w.resize(1221, 718);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newDocument(QSize(400, 300), Qt::white, QStringLiteral("parchment"));
        w.canvasView()->setZoomCentered(1.0);

        QCOMPARE(names(w), (QStringList{QStringLiteral("Paper"), QStringLiteral("Layer 1")}));
        const Layer *paper = w.layers().layer(w.layers().children(0).first());
        QVERIFY(paper->locked);
        QCOMPARE(w.layers().active()->name, QStringLiteral("Layer 1"));
        QVERIFY(!w.isModified()); // a fresh sheet isn't unsaved work

        // The picture is the paper: its colour, and not one flat colour.
        const PaperStyle &style = *paperStyle(QStringLiteral("parchment"));
        QVERIFY(near(shown(w, 120, 80), paperColorAt(style, 120, 80), 2));
        QVERIFY(near(shown(w, 300, 220), paperColorAt(style, 300, 220), 2));
        const QColor bare = shown(w, 200, 150);

        // Drawing goes on the layer over it, and erasing takes the drawing
        // off without taking the paper with it.
        inkPen(w, Qt::black, 20);
        stroke(w, {60, 150}, {340, 150});
        QVERIFY(near(shown(w, 200, 150), Qt::black));
        QVERIFY(near(pixelToColor(paper->store.pixel(200, 150)), bare, 2));
        w.brushTool()->setMode(BrushMode::Erase);
        BrushSettings eraser = w.brushTool()->settings();
        eraser.size = 60;
        eraser.hardness = 1.0;
        w.brushTool()->setSettings(eraser);
        stroke(w, {60, 150}, {340, 150});
        QVERIFY(near(shown(w, 200, 150), bare, 2));

        // The sketch commands sit over it like any other.
        QVERIFY(w.addSketchLayer());
        QCOMPARE(names(w), (QStringList{QStringLiteral("Paper"), QStringLiteral("Layer 1"), QStringLiteral("Sketch")}));
    }

    void aNewPageCanBeADifferentPaper()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.addPage(QSize(300, 200), Qt::white, QStringLiteral("kraft")));
        QCOMPARE(w.pageCount(), 2);
        QCOMPARE(w.currentPage(), 1);
        QCOMPARE(names(w), (QStringList{QStringLiteral("Paper"), QStringLiteral("Layer 1")}));
        QVERIFY(near(shown(w, 150, 100), paperStyle(QStringLiteral("kraft"))->base, 40));
        QVERIFY(shown(w, 150, 100).lightness() < 200);
        // The first page is as it was; an unknown paper is a plain background.
        QVERIFY(w.setCurrentPage(0));
        QCOMPARE(names(w), QStringList{QStringLiteral("Background")});
        QVERIFY(w.addPage(QSize(300, 200), Qt::white, QStringLiteral("no-such-paper")));
        QCOMPARE(names(w), QStringList{QStringLiteral("Background")});
    }

    void theDialogOffersThePapersAndRemembersTheLast()
    {
        {
            NewDocumentDialog dialog(QSize(800, 600));
            auto *box = dialog.findChild<QComboBox *>(QStringLiteral("newBackground"));
            QVERIFY(box);
            QCOMPARE(box->currentText(), QStringLiteral("White")); // nothing remembered yet
            QVERIFY(dialog.paper().isEmpty());
            QCOMPARE(dialog.background(), QColor(Qt::white));
            for (const PaperStyle &p : paperStyles()) {
                const int row = box->findText(p.name + QStringLiteral(" paper"));
                QVERIFY2(row >= 0, qPrintable(p.name));
                QVERIFY(!box->itemIcon(row).isNull());
            }
            box->setCurrentIndex(box->findText(QStringLiteral("Sepia paper")));
            QCOMPARE(dialog.paper(), QStringLiteral("sepia"));
            box->setCurrentIndex(1);
            QVERIFY(dialog.paper().isEmpty());
            QCOMPARE(dialog.background().alpha(), 0);
            box->setCurrentIndex(box->findText(QStringLiteral("Cotton paper")));
            auto *buttons = dialog.findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            buttons->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        NewDocumentDialog again(QSize(800, 600));
        QCOMPARE(again.paper(), QStringLiteral("cotton"));
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
