#include "brushpresets.h"
#include "brushtool.h"
#include "canvasview.h"
#include "mainwindow.h"

#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

using namespace easeletch;

namespace {

void saveShot(QWidget &w, const char *name)
{
    const QString dir = qEnvironmentVariable("EASELETCH_TEST_SHOTS");
    if (!dir.isEmpty())
        w.grab().save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
}

void setupWindow(MainWindow &w)
{
    w.resize(1221, 718);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::white);
    w.canvasView()->setZoomCentered(1.0);
    w.brushTool()->setColor(Qt::black);
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

// How much of a patch of the picture is still bare paper.
double paperShowing(MainWindow &w, const QRect &area)
{
    w.canvasView()->refresh();
    const TileStore picture = w.layers().composite();
    int bare = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (pixelToColor(picture.pixel(x, y)).lightness() > 235)
                ++bare;
    return double(bare) / (area.width() * area.height());
}

// How far apart the lightest and darkest pixels of a patch are, 0..255:
// nothing for flat ink, a lot where paper shows through a mark.
int toneRange(MainWindow &w, const QRect &area)
{
    w.canvasView()->refresh();
    const TileStore picture = w.layers().composite();
    int low = 255, high = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const int l = pixelToColor(picture.pixel(x, y)).lightness();
            low = qMin(low, l);
            high = qMax(high, l);
        }
    return high - low;
}

QListWidgetItem *rowFor(QListWidget *list, const QString &id)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id)
            return list->item(i);
    return nullptr;
}

} // namespace

class TestBrushUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("EaseletchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_brushui"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void theBrushButtonNamesTheBrushAndListsThemAll()
    {
        MainWindow w;
        setupWindow(w);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("brushButton"));
        auto *list = w.findChild<QListWidget *>(QStringLiteral("brushPresetList"));
        QVERIFY(button && list);
        QCOMPARE(w.brushTool()->preset(), defaultBrushPresetId());
        QCOMPARE(button->text(), QStringLiteral("Round brush"));

        // Every brush has a row with a picture of its stroke, under its heading.
        int rows = 0, headings = 0;
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem *item = list->item(i);
            if (item->data(Qt::UserRole).toString().isEmpty()) {
                QVERIFY(!(item->flags() & Qt::ItemIsSelectable));
                ++headings;
            } else {
                QVERIFY(!item->icon().isNull());
                QVERIFY(item->sizeHint().height() >= 32); // a target a pen can hit
                ++rows;
            }
        }
        QCOMPARE(rows, int(brushPresets().size()));
        QCOMPARE(headings, 4);
        // The whole list fits a 12" tablet's screen without scrolling.
        QVERIFY2(list->height() <= 680, qPrintable(QString::number(list->height())));
        QVERIFY(rowFor(list, QStringLiteral("knife")) && rowFor(list, QStringLiteral("flat")));
        QCOMPARE(list->currentItem(), rowFor(list, defaultBrushPresetId()));
    }

    void pressingTheButtonAgainOpensTheListAndARowChoosesABrush()
    {
        MainWindow w;
        setupWindow(w);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("brushButton"));
        auto *menu = w.findChild<QMenu *>(QStringLiteral("brushPresetMenu"));
        auto *list = w.findChild<QListWidget *>(QStringLiteral("brushPresetList"));
        auto *tools = w.findChild<QToolBar *>(QStringLiteral("ToolsBar"));
        QVERIFY(button && menu && list && tools);
        const QPoint onText(12, button->height() / 2); // the name, not the arrow

        // From another tool, the button just takes up the brush.
        for (QAction *a : tools->actions())
            if (a->text() == QStringLiteral("Lasso"))
                a->trigger();
        QVERIFY(w.canvasView()->tool() != w.brushTool());
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, onText);
        QCOMPARE(w.canvasView()->tool(), static_cast<CanvasTool *>(w.brushTool()));
        QVERIFY(!menu->isVisible());

        // Pressed again, it opens the list. A row is chosen with a click.
        bool opened = false;
        QTimer::singleShot(200, &w, [&] {
            opened = menu->isVisible();
            saveShot(w, "brush-list-window");
            saveShot(*menu, "brush-list");
            QListWidgetItem *row = rowFor(list, QStringLiteral("charcoal"));
            QVERIFY(row);
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(row).center());
        });
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, onText); // returns when the list closes
        QVERIFY(opened);
        QVERIFY(!menu->isVisible());
        QCOMPARE(w.brushTool()->preset(), QStringLiteral("charcoal"));
        QCOMPARE(button->text(), QStringLiteral("Charcoal"));
        QCOMPARE(w.brushTool()->settings().grain, brushPreset(QStringLiteral("charcoal"))->settings.grain);
        QCOMPARE(w.brushTool()->settings().size, brushPreset(QStringLiteral("charcoal"))->settings.size);

        // The options bar says which brush it is, and "More" has its grain.
        auto *bar = w.findChild<QToolBar *>(QStringLiteral("ToolOptionsBar"));
        QVERIFY(bar);
        bool named = false;
        for (QLabel *label : bar->findChildren<QLabel *>())
            named = named || label->text() == QStringLiteral("Charcoal");
        QVERIFY(named);
        auto *grain = w.findChild<QSpinBox *>(QStringLiteral("brushGrain"));
        QVERIFY(grain);
        QCOMPARE(grain->value(), 80);
        grain->setValue(30);
        QCOMPARE(w.brushTool()->settings().grain, 0.3);

        // A heading does nothing.
        w.brushTool()->setPreset(QStringLiteral("ink-fine"));
        QCOMPARE(button->text(), QStringLiteral("Fineliner"));
        QCOMPARE(list->currentItem(), rowFor(list, QStringLiteral("ink-fine")));
    }

    void choosingABrushFromAnotherToolTakesItUp()
    {
        MainWindow w;
        setupWindow(w);
        w.brushTool()->setMode(BrushMode::Erase);
        const double eraser = w.brushTool()->settings().size;
        QVERIFY(w.chooseBrushPreset(QStringLiteral("pencil-2b")));
        QCOMPARE(w.brushTool()->mode(), BrushMode::Paint);
        QCOMPARE(w.brushTool()->settings().size, 6.0);
        // The eraser's own settings are untouched.
        w.brushTool()->setMode(BrushMode::Erase);
        QCOMPARE(w.brushTool()->settings().size, eraser);
        QCOMPARE(w.brushTool()->settings().grain, 0.0);
        QVERIFY(!w.chooseBrushPreset(QStringLiteral("no-such-brush")));
    }

    void charcoalShowsThePaperAndInkDoesNot()
    {
        MainWindow w;
        setupWindow(w);
        const QRect body(120, 92, 160, 16);

        QVERIFY(w.chooseBrushPreset(QStringLiteral("charcoal")));
        stroke(w, {60, 100}, {340, 100});
        saveShot(w, "brush-charcoal-stroke");
        // Even pressed as hard as a mouse presses, the paper breaks it up.
        QVERIFY2(toneRange(w, body) > 120, qPrintable(QString::number(toneRange(w, body))));
        QVERIFY(paperShowing(w, body) < 0.9);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Brush"));

        QVERIFY(w.chooseBrushPreset(QStringLiteral("ink-marker")));
        stroke(w, {60, 200}, {340, 200});
        QCOMPARE(paperShowing(w, QRect(120, 192, 160, 16)), 0.0);
        QCOMPARE(toneRange(w, QRect(120, 192, 160, 16)), 0);
    }

    void theBrushIsRememberedAsItWasLeft()
    {
        {
            MainWindow w;
            setupWindow(w);
            QVERIFY(w.chooseBrushPreset(QStringLiteral("pencil-6b")));
            BrushSettings b = w.brushTool()->settings();
            b.size = 17; // changed after choosing it
            w.brushTool()->setSettings(b);
            w.close();
        }
        MainWindow w;
        setupWindow(w);
        QCOMPARE(w.brushTool()->preset(), QStringLiteral("pencil-6b"));
        QCOMPARE(w.brushTool()->settings().size, 17.0);
        QCOMPARE(w.brushTool()->settings().grain, brushPreset(QStringLiteral("pencil-6b"))->settings.grain);
        QCOMPARE(w.brushTool()->settings().minSize, 0.5);
        QCOMPARE(w.findChild<QToolButton *>(QStringLiteral("brushButton"))->text(), QStringLiteral("6B pencil"));
        w.close();
    }

    void undoAndRedoAreOnTheToolStrip()
    {
        MainWindow w;
        setupWindow(w);
        auto *undo = w.findChild<QToolButton *>(QStringLiteral("stripUndo"));
        auto *redo = w.findChild<QToolButton *>(QStringLiteral("stripRedo"));
        auto *tools = w.findChild<QToolBar *>(QStringLiteral("ToolsBar"));
        QVERIFY(undo && redo && tools);
        QVERIFY(undo->isVisible() && redo->isVisible());
        QVERIFY(tools->isAncestorOf(undo) && tools->isAncestorOf(redo));
        // On the strip in full, on a screen this short.
        QVERIFY(tools->rect().contains(QRect(redo->mapTo(tools, QPoint(0, 0)), redo->size())));
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
        saveShot(w, "strip-undo");

        QVERIFY(w.chooseBrushPreset(QStringLiteral("ink-tech")));
        stroke(w, {60, 100}, {340, 100});
        stroke(w, {60, 200}, {340, 200});
        QVERIFY(undo->isEnabled() && !redo->isEnabled());
        QCOMPARE(undo->text(), QStringLiteral("Undo")); // one word, whatever was done

        QTest::mouseClick(undo, Qt::LeftButton);
        QCOMPARE(paperShowing(w, QRect(120, 198, 160, 4)), 1.0);
        QCOMPARE(paperShowing(w, QRect(120, 98, 160, 4)), 0.0);
        QVERIFY(redo->isEnabled());
        QTest::mouseClick(undo, Qt::LeftButton);
        QCOMPARE(paperShowing(w, QRect(120, 98, 160, 4)), 1.0);
        QVERIFY(!undo->isEnabled());

        QTest::mouseClick(redo, Qt::LeftButton);
        QTest::mouseClick(redo, Qt::LeftButton);
        QCOMPARE(paperShowing(w, QRect(120, 198, 160, 4)), 0.0);
        QVERIFY(!redo->isEnabled());
    }
};

QTEST_MAIN(TestBrushUi)
#include "tst_brushui.moc"
