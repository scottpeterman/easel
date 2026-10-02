#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "layerpanel.h"
#include "mainwindow.h"
#include "selecttools.h"
#include "textpanel.h"

#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>

using namespace easeletch;

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
        QVERIFY(w.saveDocumentTo(dir.filePath(QStringLiteral("a.easeletch")), true));
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
        const QString path = dir.filePath(QStringLiteral("layers.easeletch"));
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

    // --- Lasso, feather, masks ---

    void lassoDragSelectsWhatItSurrounds()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        view->setTool(w.lassoTool());

        // One drag around a triangle; letting go closes it.
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 100}));
        for (const QPointF p : {QPointF(150, 100), QPointF(200, 100), QPointF(200, 150), QPointF(200, 200)})
            QTest::mouseMove(view, viewPos(w, p));
        QVERIFY(w.lassoTool()->isOpen());
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 200}));
        QVERIFY(!w.lassoTool()->isOpen());
        QVERIFY(w.selection().contains(180, 120));
        QVERIFY(!w.selection().contains(120, 180)); // the other side of the diagonal
        QVERIFY(!w.selection().contains(90, 90));

        // Shift adds a second shape; Ctrl takes one away.
        QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, viewPos(w, {300, 200}));
        for (const QPointF p : {QPointF(350, 200), QPointF(350, 250), QPointF(300, 250)})
            QTest::mouseMove(view, viewPos(w, p));
        emit w.lassoTool()->finished(w.lassoTool()->path(), Qt::ShiftModifier);
        w.lassoTool()->cancel();
        QVERIFY(w.selection().contains(325, 225));
        QVERIFY(w.selection().contains(180, 120));
        emit w.lassoTool()->finished(QPolygonF{QPointF(290, 190), QPointF(360, 190), QPointF(360, 260),
                                               QPointF(290, 260)},
                                     Qt::ControlModifier);
        QVERIFY(!w.selection().contains(325, 225));
        QVERIFY(w.selection().contains(180, 120));
    }

    void lassoClicksMakeAPolygon()
    {
        MainWindow w;
        setupWindow(w);
        CanvasView *view = w.canvasView();
        view->setTool(w.lassoTool());
        for (const QPointF p : {QPointF(50, 50), QPointF(150, 50), QPointF(150, 150), QPointF(50, 150)})
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, p));
        QVERIFY(w.lassoTool()->isOpen()); // clicks only place points
        QVERIFY(w.selection().isEmpty());
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {51, 51})); // back on the first point
        QVERIFY(!w.lassoTool()->isOpen());
        // QTest clicks land on whole view pixels, so allow a pixel either way.
        const QRect made = w.selection().bounds();
        QVERIFY(qAbs(made.left() - 50) <= 1 && qAbs(made.top() - 50) <= 1);
        QVERIFY(qAbs(made.width() - 100) <= 1 && qAbs(made.height() - 100) <= 1);
        QVERIFY(w.selection().contains(100, 100));

        // Escape gives up a path that's been started; switching tools does too.
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 200}));
        QVERIFY(w.lassoTool()->isOpen());
        w.lassoTool()->cancel();
        QVERIFY(!w.lassoTool()->isOpen());
        QCOMPARE(w.selection().bounds(), made); // the selection stays
    }

    void featheredSelectionSoftensPaintAndDelete()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(100, 50, 200, 200)));
        w.featherSelection(10);
        QVERIFY(w.selection().isSoft());
        stroke(w, {50, 150}, {200, 150});
        QCOMPARE(shown(w, 70, 150), QColor(Qt::white)); // outside: nothing
        QCOMPARE(shown(w, 180, 150), QColor(Qt::red));  // inside: full
        const QColor edge = shown(w, 100, 150);         // on the old edge: about half
        QVERIFY(edge.red() == 255 && edge.green() > 150 && edge.green() < 230);
        QVERIFY(w.history().count() == 1);              // feathering itself isn't a document change
    }

    void maskPaintingHidesAndUndoes()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.addLayer();
        const int top = w.layers().activeId();
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::blue));
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));

        QVERIFY(w.addLayerMask());
        QVERIFY(w.isEditingMask());
        QVERIFY(w.layers().layer(top)->hasMask);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue)); // a new mask shows everything
        QCOMPARE(w.layerPanel()->tree()->topLevelItem(0)->checkState(2), Qt::Checked);

        // Black on the mask cuts through to the background; the pixels stay.
        w.brushTool()->setColor(Qt::black);
        stroke(w, {50, 100}, {300, 100});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(near(drawn(w, {150.5, 100.5}), Qt::white));
        QCOMPARE(shown(w, 150, 200), QColor(Qt::blue));
        QCOMPARE(at(w.layers().layer(top)->store, 150, 100), QColor(Qt::blue));
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Brush"));

        // White brings it back.
        w.brushTool()->setColor(Qt::white);
        stroke(w, {140, 100}, {160, 100});
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QCOMPARE(shown(w, 250, 100), QColor(Qt::white));
        w.undo();
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));

        // Back on the layer, paint goes to the pixels (and is still masked).
        w.setEditingMask(false);
        QVERIFY(!w.isEditingMask());
        w.brushTool()->setColor(Qt::red);
        stroke(w, {50, 200}, {300, 200});
        QCOMPARE(shown(w, 150, 200), QColor(Qt::red));
        QCOMPARE(at(w.layers().layer(top)->store, 150, 200), QColor(Qt::red));

        // Unticking the mask shows the whole layer; applying it erases for real.
        w.setLayerMaskEnabled(top, false);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        w.undo();
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        QVERIFY(w.applyLayerMask());
        QVERIFY(!w.layers().layer(top)->hasMask);
        QCOMPARE(at(w.layers().layer(top)->store, 150, 100).alpha(), 0);
        QCOMPARE(shown(w, 150, 100), QColor(Qt::white));
        w.undo();
        QVERIFY(w.layers().layer(top)->hasMask);
        QCOMPARE(at(w.layers().layer(top)->store, 150, 100), QColor(Qt::blue));
        QVERIFY(w.deleteLayerMask());
        QCOMPARE(shown(w, 150, 100), QColor(Qt::blue));
        QCOMPARE(w.layers().layer(bg)->store.tileCount(), 0);
    }

    void maskFromSelectionShowsOnlyTheSelection()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("masked.easeletch"));
        MainWindow w;
        setupWindow(w);
        w.addLayer();
        const int top = w.layers().activeId();
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::blue));
        w.setSelection(Selection::ellipse(QRect(100, 50, 200, 200)));
        w.featherSelection(8);
        QVERIFY(w.addLayerMask());
        w.deselect();
        QCOMPARE(shown(w, 200, 150), QColor(Qt::blue));  // inside the ellipse
        QCOMPARE(shown(w, 20, 20), QColor(Qt::white));   // outside
        const QColor edge = shown(w, 100, 150);          // the feathered rim
        QVERIFY(edge.blue() == 255 && edge.red() > 60 && edge.red() < 240);

        // Switching layers goes back to painting pixels.
        const int bg = w.layers().children(0).first();
        w.setActiveLayer(bg);
        QVERIFY(!w.isEditingMask());
        w.setActiveLayer(top);
        QVERIFY(!w.isEditingMask());

        QVERIFY(w.saveDocumentTo(path, true));
        MainWindow w2;
        w2.resize(1200, 800);
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        QSignalSpy opened(&w2, &MainWindow::documentOpened);
        w2.openDocument(path);
        QVERIFY(opened.wait(10000));
        QVERIFY(w2.layers().layer(top)->hasMask);
        QCOMPARE(shown(w2, 200, 150), QColor(Qt::blue));
        QCOMPARE(shown(w2, 20, 20), QColor(Qt::white));
        QCOMPARE(shown(w2, 100, 150), edge);
    }

    void cropKeepsMasksAligned()
    {
        MainWindow w;
        setupWindow(w);
        w.addLayer();
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::blue));
        w.setSelection(Selection::rect(QRect(200, 100, 100, 100)));
        w.addLayerMask();
        w.setSelection(Selection::rect(QRect(150, 50, 200, 200)));
        w.cropToSelection();
        QCOMPARE(w.canvasSize(), QSize(200, 200));
        QCOMPARE(shown(w, 100, 100), QColor(Qt::blue));  // was (250, 150): inside the mask
        QCOMPARE(shown(w, 20, 20), QColor(Qt::white));   // was (170, 70): outside it
        w.undo();
        QCOMPARE(shown(w, 250, 150), QColor(Qt::blue));
        QCOMPARE(shown(w, 170, 70), QColor(Qt::white));
    }

    // --- Text ---

    void textLandsOnItsOwnLayerAsOneUndoStep()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(60);
        w.textPanel()->setBold(true);
        w.textPanel()->setAlignment(Qt::AlignLeft);
        w.textPanel()->setSmooth(true);

        // A real click with the Text tool starts it, once the click is over.
        w.canvasView()->setTool(w.textTool());
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {50, 100}));
        QTRY_VERIFY(w.isTyping());
        QVERIFY(w.textPanel()->isVisible());
        QCOMPARE(w.layers().count(), 2);       // a layer is ready for it
        QCOMPARE(w.history().count(), 0);      // but nothing is recorded yet
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 2);
        QVERIFY(!w.isModified());

        // Typing shows on the canvas straight away, on the new layer only.
        w.textPanel()->setText(QStringLiteral("HELLO"));
        const int text = w.layers().activeId();
        const auto inkIn = [&](const QRect &r) {
            int n = 0;
            w.canvasView()->refresh();
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    n += at(w.layers().composite(), x, y).red() < 128;
            return n;
        };
        QVERIFY(inkIn(QRect(50, 100, 250, 70)) > 500);
        QCOMPARE(inkIn(QRect(50, 200, 250, 70)), 0);
        QCOMPARE(w.layers().layer(bg)->store.tileCount(), 0);
        QVERIFY(w.selection().isEmpty()); // the frame around it isn't a selection

        // The colour follows the Color panel; more text redraws it.
        w.colorPanel()->setColor(Qt::red);
        QCOMPARE(inkIn(QRect(50, 100, 250, 70)), 0); // red, not dark, now
        w.colorPanel()->setColor(Qt::black);
        const int before = inkIn(QRect(50, 100, 340, 70));
        w.textPanel()->setText(QStringLiteral("HELLO!!"));
        QVERIFY(inkIn(QRect(50, 100, 340, 70)) > before);

        // Dragging on the canvas moves it.
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 120}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {100, 170}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {100, 220}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 220}));
        QTest::qWait(20);
        QVERIFY(w.isTyping()); // a drag moves the text; it doesn't start another
        QCOMPARE(inkIn(QRect(50, 100, 250, 60)), 0);
        QVERIFY(inkIn(QRect(50, 200, 250, 70)) > 500);
        // ... and stays moved when the text changes again.
        w.textPanel()->setText(QStringLiteral("HELLO"));
        QCOMPARE(inkIn(QRect(50, 100, 250, 60)), 0);
        QVERIFY(inkIn(QRect(50, 200, 250, 70)) > 500);

        // Placing it is one undo step, and the layer is named after the words.
        w.commitText();
        QVERIFY(!w.isTyping());
        QVERIFY(!w.textPanel()->isVisible());
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Text"));
        QCOMPARE(w.layers().layer(text)->name, QStringLiteral("HELLO"));
        QVERIFY(w.isModified());
        QVERIFY(inkIn(QRect(50, 200, 250, 70)) > 500);
        QVERIFY(w.layers().layer(text)->store.tileCount() > 0);

        w.undo();
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(inkIn(QRect(50, 200, 250, 70)), 0);
        QVERIFY(!w.isModified());
        w.redo();
        QCOMPARE(w.layers().count(), 2);
        QVERIFY(inkIn(QRect(50, 200, 250, 70)) > 500);
        // It's ordinary pixels now: the eraser works on it.
        QVERIFY(w.layer() == &w.layers().layer(text)->store);
    }

    void textCanBeDroppedOrPlacedByOtherActions()
    {
        MainWindow w;
        setupWindow(w);
        w.textPanel()->setPixelSize(40);

        // Cancel: no layer, no history.
        w.beginText({50, 50});
        w.textPanel()->setText(QStringLiteral("gone"));
        QCOMPARE(w.layers().count(), 2);
        w.cancelText();
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.history().count(), 0);
        QVERIFY(!w.isModified());
        QCOMPARE(shown(w, 60, 70), QColor(Qt::white));
        QCOMPARE(w.layerPanel()->tree()->topLevelItemCount(), 1);

        // Nothing typed, then placed: the same as cancel.
        w.beginText({50, 50});
        w.commitText();
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.history().count(), 0);

        // Undo while typing drops the text and nothing else.
        stroke(w, {50, 250}, {300, 250});
        w.beginText({50, 50});
        w.textPanel()->setText(QStringLiteral("typing"));
        w.undo();
        QVERIFY(!w.isTyping());
        QCOMPARE(w.layers().count(), 1);
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().position(), 1); // the stroke is still there
        QCOMPARE(shown(w, 150, 250), QColor(Qt::red));

        // Switching to another tool, or saving, places it.
        w.beginText({50, 50});
        w.textPanel()->setText(QStringLiteral("kept"));
        w.setActiveLayer(w.layers().children(0).first());
        QVERIFY(!w.isTyping());
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Text"));

        // Centred text is centred on the point clicked.
        w.textPanel()->setAlignment(Qt::AlignHCenter);
        w.colorPanel()->setColor(Qt::black);
        w.beginText({200, 150});
        w.textPanel()->setText(QStringLiteral("MMMM"));
        w.commitText();
        const TileStore &t = w.layers().active()->store;
        int left = 400, right = 0;
        for (int x = 0; x < 400; ++x)
            for (int y = 150; y < 200; ++y)
                if (at(t, x, y).alpha() > 128) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                }
        QVERIFY(right > left);
        QVERIFY(qAbs((left + right) / 2 - 200) <= 4);
    }

    void textToolDragMovesPlacedText()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(60);
        w.textPanel()->setBold(true);
        w.textPanel()->setAlignment(Qt::AlignLeft);
        w.canvasView()->setTool(w.textTool());
        w.beginText({50, 50});
        w.textPanel()->setText(QStringLiteral("MOVE"));
        w.commitText();
        const int text = w.layers().activeId();
        const auto inkIn = [&](const QRect &r) {
            int n = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    n += at(w.layers().layer(text)->store, x, y).alpha() > 128;
            return n;
        };
        const int ink = inkIn(QRect(50, 50, 250, 70));
        QVERIFY(ink > 500);
        const qsizetype steps = w.history().count();

        // Dragging with the Text tool moves the layer; it doesn't start new text.
        CanvasView *view = w.canvasView();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 80}));
        QTest::mouseMove(view, viewPos(w, {100, 130}));
        QTest::mouseMove(view, viewPos(w, {100, 180}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 180}));
        QTest::qWait(30);
        QVERIFY(!w.isTyping());
        QVERIFY(!w.isFloating());
        QCOMPARE(inkIn(QRect(50, 50, 250, 60)), 0);
        QCOMPARE(inkIn(QRect(50, 150, 250, 70)), ink); // the same pixels, 100 lower
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Move"));
        QVERIFY(w.selection().isEmpty());
        // Only the tiles under the text exist: moving didn't fill the canvas.
        QVERIFY(!w.layers().layer(text)->store.hasTile(TileStore::tileAt(390, 290)));
        QVERIFY(!w.layers().layer(text)->store.hasTile(TileStore::tileAt(390, 10)));
        QCOMPARE(w.layers().count(), 2);
        w.undo();
        QCOMPARE(inkIn(QRect(50, 50, 250, 70)), ink);

        // A click still starts new text.
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {200, 220}));
        QTRY_VERIFY(w.isTyping());
        w.cancelText();
        QCOMPARE(w.layers().count(), 2);

        // A locked layer doesn't move.
        w.setLayerLocked(text, true);
        const qsizetype locked = w.history().count();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 80}));
        QTest::mouseMove(view, viewPos(w, {100, 180}));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 180}));
        QTest::qWait(30);
        QCOMPARE(w.history().count(), locked);
        QCOMPARE(inkIn(QRect(50, 50, 250, 70)), ink);
        QVERIFY(!w.isTyping());
    }

    void deleteKeyInTheLayerListDeletesTheLayer()
    {
        MainWindow w;
        setupWindow(w);
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        w.addLayer();
        w.addLayer();
        QCOMPARE(w.layers().count(), 3);
        QTreeWidget *tree = w.layerPanel()->tree();

        // With the canvas focused and nothing selected, Delete does nothing.
        w.canvasView()->setFocus();
        QTest::keyClick(w.canvasView(), Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 3);

        // With the layer list focused, it deletes the active layer. That holds
        // with a selection too, when Delete would otherwise clear pixels.
        stroke(w, {50, 100}, {300, 100});
        const int painted = w.layers().activeId();
        w.setActiveLayer(w.layers().children(0).at(1));
        w.setSelection(Selection::rect(QRect(0, 0, 400, 300)));
        tree->setFocus();
        QTRY_VERIFY(tree->hasFocus());
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Delete Layer"));
        QCOMPARE(at(w.layers().layer(painted)->store, 150, 100), QColor(Qt::red)); // no pixels were cleared
        w.deselect();
        // Backspace doesn't: it stays the pixel-clearing key.
        QTest::keyClick(tree, Qt::Key_Backspace);
        QCOMPARE(w.layers().count(), 2);
        // The last layer stays.
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 1);
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 1);
        w.undo();
        QCOMPARE(w.layers().count(), 2);
    }
};

QTEST_MAIN(TestLayerUi)
#include "tst_layerui.moc"
