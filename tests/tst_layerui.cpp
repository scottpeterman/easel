#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "layerpanel.h"
#include "mainwindow.h"
#include "selecttools.h"

#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>

using namespace easel;

namespace {

QPoint viewPos(MainWindow &w, QPointF canvas)
{
    return w.canvasView()->canvasToView().map(canvas).toPoint();
}

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

// What the document looks like at a canvas pixel.
QColor shown(MainWindow &w, int x, int y)
{
    w.canvasView()->refresh();
    return at(w.layers().composite(), x, y);
}

// What the GPU actually drew there.
QColor drawn(MainWindow &w, QPointF canvas)
{
    CanvasView *view = w.canvasView();
    view->refresh();
    QImage shot;
    for (int i = 0; i < 50; ++i) {
        shot = view->grabFramebuffer();
        if (shot.isNull() || view->lastFrameStats().fallbacks == 0)
            break;
    }
    const qreal dpr = qreal(shot.width()) / qreal(view->width());
    return shot.pixelColor((view->canvasToView().map(canvas) * dpr).toPoint());
}

bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    BrushSettings b = w.brushTool()->settings();
    b.size = 20;
    b.hardness = 1.0;
    b.opacity = 1.0;
    b.flow = 1.0;
    b.stabilizer = 0.0;
    b.pixel = false;
    b.pressureOpacity = false;
    w.brushTool()->setMode(BrushMode::Paint);
    w.brushTool()->setSettings(b);
    w.brushTool()->setColor(Qt::red);
}

void stroke(MainWindow &w, QPointF from, QPointF to)
{
    CanvasView *view = w.canvasView();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, from));
    for (int i = 1; i <= 10; ++i)
        QTest::mouseMove(view, viewPos(w, from + (to - from) * (i / 10.0)));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, to));
}

int layerNamed(MainWindow &w, const QString &name)
{
    for (const Layer &l : w.layers().layers())
        if (l.name == name)
            return l.id;
    return 0;
}

} // namespace

class TestLayerUi : public QObject
{
    Q_OBJECT

private slots:
    void newDocumentHasABackgroundLayer()
    {
        MainWindow w;
        setupWindow(w);
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.layers().active()->name, QStringLiteral("Background"));
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 1);
        QCOMPARE(w.layerPanel()->tree()->currentItem()->text(0), QStringLiteral("Background"));
        QVERIFY(!w.isModified());
    }

    void paintingGoesToTheActiveLayer()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        QVERIFY(w.addLayer());
        const int top = w.layers().activeId();
        QVERIFY(top != bg);
        QCOMPARE(w.layers().active()->name, QStringLiteral("Layer 1"));
        QCOMPARE(w.layers().children(0), (QList<int>{bg, top}));
        QCOMPARE(w.layerPanel()->tree()->topLevelItem(0)->text(0), QStringLiteral("Layer 1")); // top first

        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(at(w.layers().layer(top)->store, 150, 100), QColor(Qt::red));
        QCOMPARE(w.layers().layer(bg)->store.tileCount(), 0); // untouched
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::red));
        QCOMPARE(w.history().count(), 2);
        QCOMPARE(w.history().label(0), QStringLiteral("New Layer"));
        QCOMPARE(w.history().label(1), QStringLiteral("Brush"));

        // Hiding the layer hides the stroke, on screen too.
        w.setLayerVisible(top, false);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::white));
        w.undo();
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::red));

        // Erasing on the top layer shows the background through.
        w.brushTool()->setMode(BrushMode::Erase);
        BrushSettings e = w.brushTool()->settings();
        e.size = 20;
        e.hardness = 1.0;
        e.opacity = 1.0;
        w.brushTool()->setSettings(e);
        stroke(w, {150, 80}, {150, 120});
        QCOMPARE(at(w.layers().layer(top)->store, 150, 100).alpha(), 0);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::white));
        QVERIFY(near(drawn(w, {100.5, 100.5}), Qt::red));
    }

    void undoWalksBackThroughLayerChanges()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.addLayer();
        const int top = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        QVERIFY(w.deleteLayer());
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.layers().activeId(), bg);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));

        w.undo(); // the layer and its stroke are back
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.layers().activeId(), top);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::red));

        // Painting still lands on the restored layer.
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {50, 200}, {300, 200});
        QCOMPARE(at(w.layers().layer(top)->store, 150, 200), QColor(Qt::blue));
        w.undo();
        QCOMPARE(at(w.layers().layer(top)->store, 150, 200).alpha(), 0);

        w.undo(); // the first stroke
        w.undo(); // the new layer
        QCOMPARE(w.layers().count(), 1);
        QVERIFY(!w.isModified());
        w.redo();
        w.redo();
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 2);
    }

    void lockedHiddenAndGroupLayersRefuseEdits()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.setLayerLocked(bg, true);
        const qsizetype steps = w.history().count();
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(w.history().count(), steps);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("locked")));

        w.setLayerLocked(bg, false);
        w.setLayerVisible(bg, false);
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(w.history().count(), steps + 2);
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("hidden")));
        w.setLayerVisible(bg, true);

        // A layer inside a locked group is locked too.
        QVERIFY(w.addGroup());
        const int group = w.layers().layer(bg)->parent;
        QVERIFY(group != 0);
        w.setLayerLocked(group, true);
        const qsizetype before = w.history().count();
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(w.history().count(), before);
        // With the group itself active there's nothing to paint on.
        w.setLayerLocked(group, false);
        w.setActiveLayer(group);
        QVERIFY(!w.layer());
        stroke(w, {50, 100}, {300, 100});
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("group")));
        w.setActiveLayer(bg);
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
    }

    void opacityDragIsOneUndoStep()
    {
        MainWindow w;
        setupWindow(w);
        w.addLayer();
        const int top = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        const qsizetype steps = w.history().count();
        for (int v = 95; v >= 50; v -= 5)
            w.setLayerOpacity(top, v / 100.0);
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().label(steps), QStringLiteral("Layer Opacity"));
        QVERIFY(near(shown(w, 150, 100), QColor(255, 188, 188)));
        QVERIFY(near(drawn(w, {150.5, 100.5}), QColor(255, 188, 188), 3));
        w.undo();
        QCOMPARE(w.layers().layer(top)->opacity, 1.0);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));

        w.setLayerBlend(top, BlendMode::Multiply);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red)); // red x white
        QVERIFY(w.history().undoLabel().contains(QStringLiteral("Multiply")));
    }

    void savingBetweenOpacityChangesStillMarksModified()
    {
        QTemporaryDir dir;
        MainWindow w;
        setupWindow(w);
        w.addLayer();
        const int top = w.layers().activeId();
        w.setLayerOpacity(top, 0.8);
        QVERIFY(w.saveDocumentTo(dir.filePath(QStringLiteral("a.easel")), true));
        QVERIFY(!w.isModified());
        w.setLayerOpacity(top, 0.6);
        QVERIFY(w.isModified());
    }

    void orderGroupsAndMerging()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.addLayer();
        const int a = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        w.addLayer();
        const int b = w.layers().activeId();
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {150, 50}, {150, 250});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));

        QVERIFY(w.lowerLayer()); // blue under red
        QCOMPARE(w.layers().children(0), (QList<int>{bg, b, a}));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QVERIFY(w.raiseLayer());
        QVERIFY(!w.raiseLayer()); // already on top
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));

        // Group the blue layer; raising it past the top of the group takes it out.
        QVERIFY(w.addGroup());
        const int group = w.layers().layer(b)->parent;
        QCOMPARE(w.layers().children(0), (QList<int>{bg, a, group}));
        QCOMPARE(w.layers().activeId(), b);
        QCOMPARE(w.layerPanel()->tree()->topLevelItem(0)->childCount(), 1);
        QVERIFY(w.raiseLayer());
        QCOMPARE(w.layers().children(0), (QList<int>{bg, a, group, b}));
        w.undo();
        QCOMPARE(w.layers().children(group), (QList<int>{b}));

        // Merging the group leaves one layer that looks the same.
        w.setActiveLayer(group);
        QVERIFY(w.mergeDown());
        QVERIFY(!w.layers().layer(group)->group);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QVERIFY(w.mergeDown()); // into the red layer
        QCOMPARE(w.layers().children(0), (QList<int>{bg, a}));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QCOMPARE(shown(w, 100, 100), QColor(Qt::red));

        QVERIFY(w.duplicateLayer());
        QCOMPARE(w.layers().count(), 3);
        QVERIFY(w.flattenImage());
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QCOMPARE(shown(w, 5, 5), QColor(Qt::white));
        w.undo();
        QCOMPARE(w.layers().count(), 3);

        // The last layer can't be deleted.
        w.flattenImage();
        QVERIFY(!w.deleteLayer());
        QCOMPARE(w.layers().count(), 1);
    }

    void panelDrivesTheDocument()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.addLayer();
        const int top = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        QTreeWidget *tree = w.layerPanel()->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);

        // Clicking a row makes it the active layer.
        tree->setCurrentItem(tree->topLevelItem(1));
        QCOMPARE(w.layers().activeId(), bg);
        tree->setCurrentItem(tree->topLevelItem(0));
        QCOMPARE(w.layers().activeId(), top);

        // Unticking hides it.
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY(!w.layers().layer(top)->visible);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Hide Layer"));
        tree->topLevelItem(0)->setCheckState(0, Qt::Checked);
        QTRY_VERIFY(w.layers().layer(top)->visible);

        // The lock column locks, renaming renames.
        tree->topLevelItem(0)->setCheckState(1, Qt::Checked);
        QTRY_VERIFY(w.layers().layer(top)->locked);
        tree->topLevelItem(0)->setText(0, QStringLiteral("Ink"));
        QTRY_COMPARE(w.layers().layer(top)->name, QStringLiteral("Ink"));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Ink"));

        // A drag that puts the background on top.
        QVERIFY(w.rearrangeLayers({{top, 0}, {bg, 0}}));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Background"));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white)); // the opaque background covers the stroke
        QVERIFY(!w.rearrangeLayers({{top, 0}, {bg, 0}})); // no change, no undo step
        QVERIFY(!w.rearrangeLayers({{top, bg}, {bg, 0}})); // not a group
        QCOMPARE(tree->topLevelItemCount(), 2);
    }

    void editsActOnTheActiveLayerOnly()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::green));
        w.addLayer();
        const int top = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});

        // Delete inside a selection clears the top layer; the background shows.
        w.setSelection(Selection::rect(QRect(100, 80, 50, 40)));
        w.deleteSelection();
        QCOMPARE(at(w.layers().layer(top)->store, 120, 100).alpha(), 0);
        QCOMPARE(at(w.layers().layer(bg)->store, 120, 100), QColor(Qt::green));
        QCOMPARE(shown(w, 120, 100), QColor(Qt::green));
        w.undo();
        QCOMPARE(shown(w, 120, 100), QColor(Qt::red));

        // The eyedropper picks what's on screen, even through the empty part
        // of the active layer.
        QSignalSpy picked(w.eyedropperTool(), &EyedropperTool::colorPicked);
        w.eyedropperTool()->press({{200, 250}, 1.0});
        w.eyedropperTool()->release({{200, 250}, 1.0});
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.at(0).at(0).value<QColor>(), QColor(Qt::green));

        // The wand reads the active layer: clicking the stroke selects the stroke.
        w.wandTool()->setContiguous(true);
        emit w.wandTool()->clicked({150, 100}, Qt::NoModifier);
        QVERIFY(!w.selection().isEmpty());
        QVERIFY(w.selection().bounds().height() <= 24);
        QVERIFY(w.selection().bounds().width() >= 240);

        // Crop cuts every layer, and undo brings every layer back.
        w.setSelection(Selection::rect(QRect(100, 50, 200, 100)));
        w.cropToSelection();
        QCOMPARE(w.canvasSize(), QSize(200, 100));
        QCOMPARE(at(w.layers().layer(top)->store, 50, 50), QColor(Qt::red));
        QCOMPARE(at(w.layers().layer(bg)->store, 50, 10), QColor(Qt::green));
        QCOMPARE(shown(w, 50, 50), QColor(Qt::red));
        w.undo();
        QCOMPARE(w.canvasSize(), QSize(400, 300));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::red));
        QCOMPARE(shown(w, 10, 10), QColor(Qt::green));
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 2);
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {50, 200}, {300, 200}); // tools are rebound after the swap
        QCOMPARE(at(w.layers().layer(top)->store, 150, 200), QColor(Qt::blue));
        QCOMPARE(at(w.layers().layer(bg)->store, 150, 200), QColor(Qt::green));
    }

    void switchingLayersDropsFloatingPixels()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.addLayer();
        const int top = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        w.setSelection(Selection::rect(QRect(100, 80, 50, 40)));
        w.copy();
        w.paste();
        QVERIFY(w.isFloating());
        w.setActiveLayer(bg);
        QVERIFY(!w.isFloating()); // dropped onto the layer it floated over
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Paste"));
        QCOMPARE(w.layers().layer(bg)->store.tileCount(), 0);
        QVERIFY(w.layers().layer(top)->store.tileCount() > 0);
    }

    void saveAndReopenKeepsTheLayers()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layers.easel"));
        MainWindow w;
        setupWindow(w);
        w.addLayer();
        const int a = w.layers().activeId();
        stroke(w, {50, 100}, {300, 100});
        w.renameLayer(a, QStringLiteral("Ink"));
        w.addGroup();
        w.addLayer();
        const int b = w.layers().activeId();
        w.brushTool()->setColor(Qt::blue);
        stroke(w, {150, 50}, {150, 250});
        w.setLayerOpacity(b, 0.5);
        w.setLayerBlend(b, BlendMode::Multiply);
        w.setLayerLocked(b, true);
        const QColor mixed = shown(w, 150, 100);
        QVERIFY(w.saveDocumentTo(path, true));
        QVERIFY(!w.isModified());

        MainWindow w2;
        w2.resize(1200, 800);
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        QSignalSpy opened(&w2, &MainWindow::documentOpened);
        w2.openDocument(path);
        QVERIFY(opened.wait(10000));
        QCOMPARE(opened.at(0).at(1).toBool(), true);
        QCOMPARE(w2.layers().count(), 4);
        QCOMPARE(w2.layers().activeId(), b);
        const int ink = layerNamed(w2, QStringLiteral("Ink"));
        QVERIFY(ink != 0);
        const Layer *group = w2.layers().layer(w2.layers().layer(ink)->parent);
        QVERIFY(group && group->group);
        QCOMPARE(w2.layers().children(group->id).size(), 2);
        const Layer *top = w2.layers().layer(b);
        QCOMPARE(top->opacity, 0.5);
        QCOMPARE(top->blend, BlendMode::Multiply);
        QVERIFY(top->locked);
        QCOMPARE(shown(w2, 150, 100), mixed);
        QCOMPARE(shown(w2, 100, 100), QColor(Qt::red));
        QCOMPARE(w2.layerPanel()->tree()->topLevelItemCount(), 2); // the group and the background
        QVERIFY(!w2.isModified());
    }
};

QTEST_MAIN(TestLayerUi)
#include "tst_layerui.moc"
