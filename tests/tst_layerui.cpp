#include "brushtool.h"
#include "canvasview.h"
#include "colorpanel.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "layerpanel.h"
#include "mainwindow.h"
#include "selecttools.h"
#include "textpanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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

        // With nothing selected, Delete removes the highlighted layer, even
        // with the canvas focused, and says so.
        w.addLayer();
        QCOMPARE(w.layers().count(), 4);
        w.canvasView()->setFocus();
        QTest::keyClick(w.canvasView(), Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 3);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Delete Layer"));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("Deleted layer")));
        // With a selection it clears pixels and leaves the layers alone.
        w.layer()->fillRect(QRect(0, 0, 400, 300), QColor(Qt::blue));
        w.setSelection(Selection::rect(QRect(10, 10, 50, 50)));
        QTest::keyClick(w.canvasView(), Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 3);
        QCOMPARE(at(*w.layer(), 20, 20).alpha(), 0);
        QCOMPARE(at(*w.layer(), 100, 100), QColor(Qt::blue));
        w.deselect();

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
        // The last layer stays, whichever way it's asked for.
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 1);
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 1);
        w.canvasView()->setFocus();
        QTest::keyClick(w.canvasView(), Qt::Key_Delete);
        QCOMPARE(w.layers().count(), 1);
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("at least one layer")));
        w.undo();
        QCOMPARE(w.layers().count(), 2);
    }

    void placedTextCanBeChanged()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(60);
        w.textPanel()->setBold(true);
        w.textPanel()->setAlignment(Qt::AlignLeft);
        w.textPanel()->setOutline(0);
        w.canvasView()->setTool(w.textTool());
        w.beginText({40, 60});
        w.textPanel()->setText(QStringLiteral("HELLO"));
        w.commitText();
        const int text = w.layers().activeId();
        const auto layerOf = [&] { return w.layers().layer(text); };
        const auto inkIn = [&](const QRect &r) {
            int n = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    n += at(layerOf()->store, x, y).alpha() > 128;
            return n;
        };
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->text.text, QStringLiteral("HELLO"));
        QCOMPARE(layerOf()->textAnchor, QPoint(40, 60));
        const int inkHello = inkIn(QRect(0, 0, 400, 300));
        const QRect box = layerOf()->textBox;
        QVERIFY(box.contains(QPoint(100, 90)));
        QCOMPARE(w.textLayerAt({100, 90}), text);
        QCOMPARE(w.textLayerAt({100, 250}), 0);

        // The colour and font in the panels are someone else's by now.
        w.colorPanel()->setColor(Qt::red);
        w.textPanel()->setPixelSize(20);
        w.setActiveLayer(w.layers().children(0).first());

        // A click on the words with the Text tool opens them again.
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 90}));
        QTRY_VERIFY(w.isTyping());
        QCOMPARE(w.layers().count(), 2); // no new layer
        QCOMPARE(w.layers().activeId(), text);
        QCOMPARE(w.textPanel()->text(), QStringLiteral("HELLO"));
        QCOMPARE(w.textPanel()->settings().pixelSize, 60);
        QCOMPARE(w.colorPanel()->color(), QColor(Qt::black));
        QCOMPARE(inkIn(QRect(0, 0, 400, 300)), inkHello); // shown as it was

        // Escape leaves it as it was, still text.
        w.textPanel()->setText(QStringLiteral("HELLO WORLD"));
        w.cancelText();
        QCOMPARE(w.history().count(), 1);
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->text.text, QStringLiteral("HELLO"));
        QCOMPARE(inkIn(QRect(0, 0, 400, 300)), inkHello);

        // Changed and placed: one undo step, same layer, renamed after the new words.
        QVERIFY(w.editText(text));
        w.textPanel()->setText(QStringLiteral("HI"));
        w.commitText();
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.history().count(), 2);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Edit Text"));
        QCOMPARE(layerOf()->name, QStringLiteral("HI"));
        QCOMPARE(layerOf()->text.text, QStringLiteral("HI"));
        QCOMPARE(layerOf()->textAnchor, QPoint(40, 60));
        QVERIFY(layerOf()->isText());
        const int inkHi = inkIn(QRect(0, 0, 400, 300));
        QVERIFY(inkHi > 200 && inkHi < inkHello);
        w.undo();
        QCOMPARE(layerOf()->text.text, QStringLiteral("HELLO"));
        QCOMPARE(inkIn(QRect(0, 0, 400, 300)), inkHello);
        QVERIFY(layerOf()->isText());
        w.redo();
        QCOMPARE(layerOf()->text.text, QStringLiteral("HI"));

        // A name of the user's own is kept.
        w.renameLayer(text, QStringLiteral("Title"));
        QVERIFY(w.editText(text));
        w.textPanel()->setText(QStringLiteral("HELLO"));
        w.commitText();
        QCOMPARE(layerOf()->name, QStringLiteral("Title"));

        // Moving the layer moves the text with it: still text, at the new place.
        w.canvasView()->setTool(w.textTool());
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 90}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {100, 140}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {110, 190}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {110, 190}));
        QTest::qWait(20);
        QVERIFY(!w.isTyping());
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->textAnchor, QPoint(50, 160));
        QCOMPARE(inkIn(QRect(0, 0, 400, 300)), inkHello);
        QCOMPARE(inkIn(QRect(0, 0, 400, 150)), 0);
        QCOMPARE(w.textLayerAt({110, 190}), text);
        w.undo();
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->textAnchor, QPoint(40, 60));
        QCOMPARE(inkIn(QRect(0, 150, 400, 150)), 0);
        w.redo();

        // Pushed half off the canvas and brought back, nothing is lost.
        w.canvasView()->setTool(w.moveTool());
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {110, 190}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {60, 190}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {10, 190}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {10, 190}));
        w.commitFloating(); // the Move tool keeps it floating until it's placed
        QVERIFY(inkIn(QRect(0, 0, 400, 300)) < inkHello);
        QVERIFY(layerOf()->isText());
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {10, 190}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {60, 190}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {110, 190}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {110, 190}));
        w.commitFloating();
        QCOMPARE(inkIn(QRect(0, 0, 400, 300)), inkHello);
        QCOMPARE(layerOf()->textAnchor, QPoint(50, 160));
        QVERIFY(w.selection().isEmpty());

        // Cropping the canvas keeps it text, at its place in the new canvas.
        w.setSelection(Selection::rect(QRect(20, 100, 360, 180)));
        w.cropToSelection();
        QCOMPARE(w.canvasSize(), QSize(360, 180));
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->textAnchor, QPoint(30, 60));
        QCOMPARE(inkIn(QRect(0, 0, 360, 180)), inkHello);
        w.undo();
        QVERIFY(layerOf()->isText());
        QCOMPARE(layerOf()->textAnchor, QPoint(50, 160));

        // It survives a save: reopened, a click still opens the words.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("text.easeletch"));
        QVERIFY(w.saveDocumentTo(path, true));
        {
            MainWindow w2;
            w2.resize(1200, 800);
            w2.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w2));
            QSignalSpy opened(&w2, &MainWindow::documentOpened);
            w2.openDocument(path);
            QVERIFY(opened.wait(10000));
            QVERIFY(opened.first().at(1).toBool());
            const int id = w2.textLayerAt({110, 190});
            QVERIFY(id != 0);
            QVERIFY(w2.editText(id));
            QCOMPARE(w2.textPanel()->text(), QStringLiteral("HELLO"));
            QCOMPARE(w2.textPanel()->settings().pixelSize, 60);
            w2.cancelText();
        }

        // Painted on, it's ordinary pixels: there's nothing to open. Undo
        // brings the text back.
        w.canvasView()->setTool(w.brushTool());
        stroke(w, {60, 190}, {300, 190});
        QVERIFY(!layerOf()->isText());
        QCOMPARE(w.textLayerAt({110, 190}), 0);
        QVERIFY(!w.editText(text));
        QVERIFY(!w.isTyping());
        w.undo();
        QVERIFY(layerOf()->isText());
        QCOMPARE(w.textLayerAt({110, 190}), text);

        // The same for a flip: the pixels aren't what the words draw any more.
        w.flipHorizontal();
        QVERIFY(!layerOf()->isText());
        w.undo();
        QVERIFY(layerOf()->isText());

        // A locked text layer isn't opened.
        w.setLayerLocked(text, true);
        QVERIFY(!w.editText(text));
        QVERIFY(!w.isTyping());
    }

    void textColourChangesFromEitherPanel()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(60);
        w.textPanel()->setBold(true);
        w.textPanel()->setOutline(0);
        w.beginText({40, 60});
        w.textPanel()->setText(QStringLiteral("HELLO"));
        w.commitText();
        const int text = w.layers().activeId();
        const auto count = [&](const QColor &want) {
            int n = 0;
            for (int y = 0; y < 300; ++y)
                for (int x = 0; x < 400; ++x) {
                    const QColor c = at(w.layers().layer(text)->store, x, y);
                    n += c.alpha() == 255 && near(c, want);
                }
            return n;
        };
        const int ink = count(Qt::black);
        QVERIFY(ink > 500);

        // Open again, the Text window shows the text's own colour.
        w.colorPanel()->setColor(Qt::red);
        QVERIFY(w.editText(text));
        QCOMPARE(w.textPanel()->color(), QColor(Qt::black));
        // The Color panel recolours all of it, and the window's button follows.
        const QColor green(0x38, 0x8e, 0x3c);
        w.colorPanel()->setColor(green);
        QCOMPARE(w.textPanel()->color(), green);
        QCOMPARE(count(green), ink);
        QCOMPARE(count(Qt::black), 0);
        // The window's own button does the same, through the Color panel.
        emit w.textPanel()->colorPicked(Qt::blue);
        QCOMPARE(w.colorPanel()->color(), QColor(Qt::blue));
        QCOMPARE(w.textPanel()->color(), QColor(Qt::blue));
        QCOMPARE(count(Qt::blue), ink);
        w.commitText();
        QCOMPARE(w.layers().layer(text)->text.color, QColor(Qt::blue));
        QCOMPARE(count(Qt::blue), ink);
        QVERIFY(w.layers().layer(text)->isText());
        w.undo();
        QCOMPARE(count(Qt::black), ink);

        // The outline's colour button is off while there's no outline.
        auto *outline = w.textPanel()->findChild<QWidget *>(QStringLiteral("outlineColor"));
        QVERIFY(outline && !outline->isEnabled());
        w.textPanel()->setOutline(3);
        QVERIFY(outline->isEnabled());
        w.textPanel()->setOutline(0);
    }

    void framedTextStaysEditable()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(30);
        w.textPanel()->setBold(false);
        w.textPanel()->setOutline(0);
        w.textPanel()->setAlignment(Qt::AlignLeft);
        TextFrame frame;
        frame.style = FrameStyle::Double;
        frame.line = 4;
        frame.lineColor = Qt::blue;
        frame.filled = true;
        frame.fill = Qt::yellow;
        frame.padding = 24;
        w.beginText({120, 120});
        w.textPanel()->setFrame(frame);
        w.textPanel()->setText(QStringLiteral("Card"));
        w.commitText();
        const int text = w.layers().activeId();
        const Layer *l = w.layers().layer(text);
        QVERIFY(l->isText());
        QCOMPARE(l->text.frame.style, FrameStyle::Double);
        QCOMPARE(l->textAnchor, QPoint(120, 120));
        // The fill is behind the words, in the padding; the frame is part of
        // what a click hits.
        QCOMPARE(at(l->store, 108, 140), QColor(Qt::yellow));
        QVERIFY(l->textBox.contains(QPoint(100, 110)));
        QCOMPARE(w.textLayerAt({100, 110}), text);
        QCOMPARE(w.textLayerAt({40, 40}), 0);

        // The next text starts with the same frame; text with none turns it
        // off but keeps its line and colours for later.
        QCOMPARE(w.textPanel()->frame().style, FrameStyle::Double);
        w.textPanel()->setFrame(TextFrame());
        w.canvasView()->setTool(w.textTool());
        QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {100, 110}));
        QTRY_VERIFY(w.isTyping());
        QCOMPARE(w.layers().activeId(), text);
        QCOMPARE(w.textPanel()->frame().style, FrameStyle::Double);
        QCOMPARE(w.textPanel()->frame().lineColor, QColor(Qt::blue));
        QCOMPARE(w.textPanel()->frame().padding, 24);

        // Another stencil, and the frame taken off again: each one undo step.
        TextFrame looped = w.textPanel()->frame();
        looped.style = FrameStyle::Looped;
        w.textPanel()->setFrame(looped);
        w.commitText();
        QCOMPARE(w.layers().layer(text)->text.frame.style, FrameStyle::Looped);
        QVERIFY(w.layers().layer(text)->isText());
        QCOMPARE(w.layers().layer(text)->textAnchor, QPoint(120, 120)); // the words didn't move
        QVERIFY(w.editText(text));
        TextFrame off = w.textPanel()->frame();
        off.style = FrameStyle::None;
        w.textPanel()->setFrame(off);
        w.commitText();
        QVERIFY(!w.layers().layer(text)->text.frame.isActive());
        QCOMPARE(at(w.layers().layer(text)->store, 108, 140).alpha(), 0);
        w.undo();
        QCOMPARE(w.layers().layer(text)->text.frame.style, FrameStyle::Looped);
        QCOMPARE(at(w.layers().layer(text)->store, 108, 140), QColor(Qt::yellow));
        w.undo();
        QCOMPARE(w.layers().layer(text)->text.frame.style, FrameStyle::Double);
    }

    void balloonTailIsAimedByDraggingItsHandle()
    {
        MainWindow w;
        setupWindow(w);
        w.colorPanel()->setColor(Qt::black);
        w.textPanel()->setPixelSize(24);
        w.textPanel()->setBold(false);
        w.textPanel()->setOutline(0);
        w.textPanel()->setAlignment(Qt::AlignLeft);
        w.textPanel()->setFrame(TextFrame());
        w.canvasView()->setTool(w.textTool());
        w.beginText({150, 60});
        // Picked in the panel, a balloon arrives filled, with a tail.
        auto *style = w.textPanel()->findChild<QComboBox *>(QStringLiteral("frameStyle"));
        auto *tail = w.textPanel()->findChild<QCheckBox *>(QStringLiteral("frameTail"));
        QVERIFY(style && tail);
        QVERIFY(!tail->isEnabled());
        style->setCurrentIndex(style->findData(int(FrameStyle::Speech)));
        QVERIFY(tail->isEnabled() && tail->isChecked());
        QVERIFY(w.textPanel()->frame().filled);
        w.textPanel()->setText(QStringLiteral("Hello"));
        QCOMPARE(w.canvasView()->handles().size(), 1); // the tail's handle
        const QPointF tip = w.canvasView()->handles().first().boundingRect().center();
        QVERIFY(tip.y() > 90); // below the words

        // Dragging the handle aims the tail; it doesn't move the words.
        const int text = w.layers().activeId();
        const auto solidAt = [&](int x, int y) { return at(w.layers().layer(text)->store, x, y).alpha() > 200; };
        QVERIFY(!solidAt(330, 248));
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, tip));
        QTest::mouseMove(w.canvasView(), viewPos(w, {250, 200}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {330, 250}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {330, 250}));
        QTest::qWait(20);
        QVERIFY(w.isTyping());
        QCOMPARE(w.layers().count(), 2); // no new text started
        const QPointF moved = w.canvasView()->handles().first().boundingRect().center();
        QVERIFY(qAbs(moved.x() - 330.5) <= 1.0 && qAbs(moved.y() - 250.5) <= 1.0);
        bool drawn = false;
        for (int dy = -8; dy <= 0 && !drawn; ++dy)
            for (int dx = -8; dx <= 0 && !drawn; ++dx)
                drawn = solidAt(330 + dx, 250 + dy);
        QVERIFY(drawn);
        const QPoint offset = w.textPanel()->frame().tailOffset;
        QVERIFY(offset.x() > 100 && offset.y() > 100);

        // Dragging the words takes the tail with them.
        QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {170, 75}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {160, 85}));
        QTest::mouseMove(w.canvasView(), viewPos(w, {150, 95}));
        QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, {150, 95}));
        QCOMPARE(w.textPanel()->frame().tailOffset, offset);
        const QPointF after = w.canvasView()->handles().first().boundingRect().center();
        QVERIFY(qAbs(after.x() - 310.5) <= 1.0 && qAbs(after.y() - 270.5) <= 1.0);

        // Placed, the handle goes; opened again, it's back where it was.
        w.commitText();
        QVERIFY(w.canvasView()->handles().isEmpty());
        const Layer *l = w.layers().layer(text);
        QVERIFY(l->isText());
        QCOMPARE(l->text.frame.style, FrameStyle::Speech);
        QVERIFY(l->text.frame.tail);
        QCOMPARE(l->text.frame.tailOffset, offset);
        QCOMPARE(l->textAnchor, QPoint(130, 80));
        // A click on the tail out in the open doesn't open the text; one on the body does.
        QCOMPARE(w.textLayerAt({300, 262}), 0);
        QCOMPARE(w.textLayerAt({150, 95}), text);
        // The next text keeps the balloon but not where this one's tail was aimed.
        QCOMPARE(w.textPanel()->frame().style, FrameStyle::Speech);
        QCOMPARE(w.textPanel()->frame().tailOffset, QPoint());
        QVERIFY(w.editText(text));
        QCOMPARE(w.textPanel()->frame().tailOffset, offset);
        QCOMPARE(w.canvasView()->handles().size(), 1);
        const QPointF again = w.canvasView()->handles().first().boundingRect().center();
        QVERIFY(qAbs(again.x() - 310.5) <= 1.0 && qAbs(again.y() - 270.5) <= 1.0);
        // Without its tail there's no handle.
        tail->setChecked(false);
        QVERIFY(w.canvasView()->handles().isEmpty());
        w.cancelText();
        QVERIFY(w.layers().layer(text)->text.frame.tail);
        w.textPanel()->setFrame(TextFrame());
    }

    // --- Shapes ---

    void shapeIsDrawnPointByPointAndChangedAfterwards()
    {
        MainWindow w;
        setupWindow(w);
        const int bg = w.layers().activeId();
        w.colorPanel()->setColor(Qt::blue);
        MainWindow::ShapeOptions o;
        o.line = 4;
        o.lineColor = Qt::black;
        w.setShapeOptions(o);
        w.canvasView()->setTool(w.shapeTool());
        const auto click = [&](QPointF p) {
            QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, p));
            QTest::qWait(QApplication::doubleClickInterval() + 60); // separate clicks, not a double-click
        };
        const auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, from));
            QTest::mouseMove(w.canvasView(), viewPos(w, (from + to) / 2));
            QTest::mouseMove(w.canvasView(), viewPos(w, to));
            QTest::mouseRelease(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, to));
            QTest::qWait(QApplication::doubleClickInterval() + 60);
        };

        // The first click makes a layer for it; each click after adds a point.
        click({60, 60});
        QTRY_VERIFY(w.isShaping());
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.history().count(), 0);
        QCOMPARE(w.shapePoints().size(), 1);
        QCOMPARE(w.canvasView()->handles().size(), 1);
        const int shape = w.layers().activeId();
        click({260, 60});
        click({260, 200});
        QCOMPARE(w.shapePoints().size(), 3);
        QCOMPARE(w.canvasView()->handles().size(), 3);
        const auto pix = [&](int x, int y) { return at(w.layers().layer(shape)->store, x, y); };
        QCOMPARE(pix(160, 60), QColor(Qt::black)); // drawn as it goes
        QCOMPARE(w.layers().layer(bg)->store.tileCount(), 0);
        // Backspace takes the last point back.
        QTest::keyClick(w.canvasView(), Qt::Key_Backspace);
        QCOMPARE(w.shapePoints().size(), 2);
        click({260, 200});
        click({60, 200});
        QCOMPARE(w.shapePoints().size(), 4);
        // A click on the first point closes it and puts it down: one undo step.
        click({61, 61});
        QTRY_VERIFY(!w.isShaping());
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Shape"));
        QVERIFY(w.canvasView()->handles().isEmpty());
        QVERIFY(w.layers().layer(shape)->isShape());
        QCOMPARE(w.layers().layer(shape)->shape.points.size(), 4);
        QCOMPARE(pix(60, 130), QColor(Qt::black)); // the closing side
        QCOMPARE(pix(160, 130).alpha(), 0);        // not filled
        QVERIFY(w.selection().isEmpty());
        w.undo();
        QCOMPARE(w.layers().count(), 1);
        w.redo();
        QCOMPARE(w.layers().count(), 2);
        QVERIFY(w.layers().layer(shape)->isShape());

        // A click on it opens it again: its points are back as handles.
        QCOMPARE(w.shapeLayerAt({160, 61}), shape);
        QCOMPARE(w.shapeLayerAt({160, 130}), shape); // inside counts
        QCOMPARE(w.shapeLayerAt({330, 250}), 0);
        w.setActiveLayer(bg);
        click({160, 61});
        QTRY_VERIFY(w.isShaping());
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.layers().activeId(), shape);
        QCOMPARE(w.canvasView()->handles().size(), 4);
        QCOMPARE(pix(160, 60), QColor(Qt::black)); // shown as it was

        // Dragging a point moves it; the sides follow.
        drag({260, 60}, {320, 30});
        QCOMPARE(w.shapePoints().at(1), QPointF(320, 30));
        QCOMPARE(pix(160, 60).alpha(), 0);
        // Dragging a side adds a point there.
        drag({60, 130}, {30, 130});
        QCOMPARE(w.shapePoints().size(), 5);
        QCOMPARE(w.shapePoints().at(4), QPointF(30, 130));
        // Delete removes the point last touched.
        QTest::keyClick(w.canvasView(), Qt::Key_Delete);
        QCOMPARE(w.shapePoints().size(), 4);
        // Dragging inside moves all of it.
        drag({160, 150}, {170, 170});
        QCOMPARE(w.shapePoints().at(0), QPointF(70, 80));
        QCOMPARE(w.shapePoints().at(1), QPointF(330, 50));
        // The fill is the painting colour; the options change it as it stands.
        o = w.shapeOptions();
        o.filled = true;
        w.setShapeOptions(o);
        QCOMPARE(pix(170, 160), QColor(Qt::blue));
        w.colorPanel()->setColor(Qt::green);
        QCOMPARE(pix(170, 160), QColor(Qt::green));
        QVERIFY(w.isShaping());

        // Escape drops the changes; Enter keeps them, as one undo step.
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        w.canvasView()->setFocus();
        QTest::keyClick(w.canvasView(), Qt::Key_Escape);
        QVERIFY(!w.isShaping());
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.layers().layer(shape)->shape.points.at(1), QPointF(260, 60));
        QVERIFY(w.layers().layer(shape)->isShape());
        QCOMPARE(pix(160, 60), QColor(Qt::black));

        QVERIFY(w.editShape(shape));
        o = w.shapeOptions();
        QVERIFY(!o.filled); // the options show the shape as it is
        o.filled = true;
        o.curved = true;
        w.setShapeOptions(o);
        w.colorPanel()->setColor(Qt::red);
        w.canvasView()->setFocus();
        QTest::keyClick(w.canvasView(), Qt::Key_Return);
        QVERIFY(!w.isShaping());
        QCOMPARE(w.history().count(), 2);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Edit Shape"));
        const Layer *l = w.layers().layer(shape);
        QVERIFY(l->isShape() && l->shape.filled && l->shape.curved);
        QCOMPARE(l->shape.fill, QColor(Qt::red));
        QCOMPARE(pix(160, 130), QColor(Qt::red));
        w.undo();
        QVERIFY(!w.layers().layer(shape)->shape.filled);
        QCOMPARE(pix(160, 130).alpha(), 0);
        w.redo();

        // Moving the layer takes the points with it: still a shape.
        w.canvasView()->setTool(w.moveTool());
        drag({160, 130}, {180, 150});
        w.commitFloating();
        QVERIFY(w.layers().layer(shape)->isShape());
        QCOMPARE(w.layers().layer(shape)->shape.points.at(0), QPointF(80, 80));
        QCOMPARE(pix(180, 150), QColor(Qt::red));
        // ... and so does a crop.
        w.setSelection(Selection::rect(QRect(20, 20, 360, 260)));
        w.cropToSelection();
        QVERIFY(w.layers().layer(shape)->isShape());
        QCOMPARE(w.layers().layer(shape)->shape.points.at(0), QPointF(60, 60));
        w.undo();
        QCOMPARE(w.layers().layer(shape)->shape.points.at(0), QPointF(80, 80));

        // It survives a save.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shape.easeletch"));
        QVERIFY(w.saveDocumentTo(path, true));
        {
            MainWindow w2;
            w2.resize(1200, 800);
            w2.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w2));
            QSignalSpy opened(&w2, &MainWindow::documentOpened);
            w2.openDocument(path);
            QVERIFY(opened.wait(10000));
            const int id = w2.shapeLayerAt({180, 150});
            QVERIFY(id != 0);
            QVERIFY(w2.editShape(id));
            QCOMPARE(w2.shapePoints().size(), 4);
            QVERIFY(w2.shapeOptions().curved);
            w2.cancelShape();
        }

        // Painted on, it's ordinary pixels: nothing to open. Undo brings the shape back.
        w.canvasView()->setTool(w.brushTool());
        w.brushTool()->setColor(Qt::black);
        stroke(w, {100, 150}, {250, 150});
        QVERIFY(!w.layers().layer(shape)->isShape());
        QCOMPARE(w.shapeLayerAt({180, 150}), 0);
        QVERIFY(!w.editShape(shape));
        w.undo();
        QVERIFY(w.layers().layer(shape)->isShape());
    }

    void shapeFinishesOnDoubleClickOrEnterAndCanBeAnOpenLine()
    {
        MainWindow w;
        setupWindow(w);
        MainWindow::ShapeOptions o;
        o.line = 6;
        o.closed = false;
        w.setShapeOptions(o);
        w.canvasView()->setTool(w.shapeTool());
        // Through the window's own calls: an open line of three points, placed with Enter.
        w.beginShape({50, 50});
        w.addShapePoint({200, 50});
        w.addShapePoint({200, 200});
        QVERIFY(w.isShaping());
        const int line = w.layers().activeId();
        w.commitFloating(); // what Enter does
        QVERIFY(!w.isShaping());
        const Layer *l = w.layers().layer(line);
        QVERIFY(l->isShape() && !l->shape.closed);
        QCOMPARE(at(l->store, 120, 50), QColor(Qt::black));
        QCOMPARE(at(l->store, 125, 125).alpha(), 0); // no side back to the start

        // One point isn't a shape: putting it down leaves nothing behind.
        w.beginShape({300, 250});
        QCOMPARE(w.layers().count(), 3);
        w.commitShape();
        QCOMPARE(w.layers().count(), 2);
        QCOMPARE(w.history().count(), 1);
        // Backspace on the only point drops it too.
        w.beginShape({300, 250});
        QTest::keyClick(w.canvasView(), Qt::Key_Backspace);
        QVERIFY(!w.isShaping());
        QCOMPARE(w.layers().count(), 2);

        // A double-click on the last point finishes.
        const auto clickAt = [&](QPointF p) {
            QTest::mouseClick(w.canvasView(), Qt::LeftButton, Qt::NoModifier, viewPos(w, p));
        };
        clickAt({300, 60});
        QTRY_VERIFY(w.isShaping());
        QTest::qWait(QApplication::doubleClickInterval() + 60);
        clickAt({360, 120});
        QTest::qWait(QApplication::doubleClickInterval() + 60);
        clickAt({300, 180});
        QCOMPARE(w.shapePoints().size(), 3);
        clickAt({300, 180}); // again, straight away
        QTRY_VERIFY(!w.isShaping());
        QCOMPARE(w.layers().count(), 3);
        QCOMPARE(w.layers().active()->shape.points.size(), 3);

        // Switching tools puts a shape down; undo while drawing drops it.
        w.beginShape({40, 220});
        w.addShapePoint({120, 260});
        w.canvasView()->setTool(w.shapeTool());
        w.undo();
        QVERIFY(!w.isShaping());
        QCOMPARE(w.layers().count(), 3);
        w.beginShape({40, 220});
        w.addShapePoint({120, 260});
        w.setActiveLayer(w.layers().children(0).first());
        QVERIFY(!w.isShaping());
        QCOMPARE(w.layers().count(), 4);
        // A locked shape isn't opened.
        w.setLayerLocked(line, true);
        QVERIFY(!w.editShape(line));
        MainWindow::ShapeOptions back;
        w.setShapeOptions(back);
    }
};

QTEST_MAIN(TestLayerUi)
#include "tst_layerui.moc"
