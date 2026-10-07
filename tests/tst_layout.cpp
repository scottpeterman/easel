#include "brushoptionsbar.h"
#include "colorpanel.h"
#include "mainwindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QToolBar>

// The window on a small screen. A 12" tablet at 2x leaves a maximised window
// about 1220 x 720 under its title bar, and the window used to need 1490 x 780:
// held smaller than that, panels ran into each other and toolbar labels were
// cut short.

namespace {

const QSize kSmallScreen(1221, 718);
// The widest the window may insist on being, whatever tool is active.
const int kNarrowest = 900;

void showAt(MainWindow &w, QSize size)
{
    w.resize(size);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(2000, 1500), Qt::white);
    QTest::qWait(50); // docks animate into place
    QCoreApplication::processEvents();
}

// Text the widget would need more room than it has to show in full. Hints
// made to be cut short (they ignore their own width) don't count.
template <typename T>
QStringList cutShort(QToolBar *bar)
{
    QStringList out;
    for (T *w : bar->findChildren<T *>()) {
        if (!w->isVisible() || w->text().isEmpty() || w->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored)
            continue;
        if (w->width() < w->sizeHint().width())
            out << w->text();
    }
    return out;
}

void saveShot(MainWindow &w, const char *name)
{
    const QString dir = qEnvironmentVariable("EASELETCH_TEST_SHOTS");
    if (!dir.isEmpty())
        w.grab().save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
}

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // A window layout saved by the app itself must not decide these.
        QCoreApplication::setOrganizationName(QStringLiteral("EaseletchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_layout"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void colorPanelStaysInsideItsDock()
    {
        MainWindow w;
        showAt(w, kSmallScreen);
        auto *panel = w.findChild<ColorPanel *>();
        QVERIFY(panel);
        // The palette it starts with and a full Recent row must fit as they are.
        for (int i = 0; i < ColorPanel::MaxRecent; ++i)
            panel->noteUsed(QColor::fromHsv(i * 20, 255, 255));
        QTest::qWait(50);
        QCoreApplication::processEvents();
        saveShot(w, "small-color");

        auto *dock = w.findChild<QDockWidget *>(QStringLiteral("ColorDock"));
        QVERIFY(dock && dock->isVisible());
        const QRect dockRect(dock->mapToGlobal(QPoint(0, 0)), dock->size());
        const QRect panelRect(panel->mapToGlobal(QPoint(0, 0)), panel->size());
        QVERIFY2(dockRect.contains(panelRect), "the colour panel is bigger than its dock");
        auto *scroll = w.findChild<QScrollArea *>(QStringLiteral("ColorScroll"));
        QVERIFY(scroll && !scroll->verticalScrollBar()->isVisible());

        for (QWidget *child : panel->findChildren<QWidget *>()) {
            if (!child->isVisible())
                continue;
            const QRect r(child->mapToGlobal(QPoint(0, 0)), child->size());
            QVERIFY2(panelRect.contains(r), qPrintable(QStringLiteral("%1 at %2,%3 %4x%5 leaves the colour panel")
                                                           .arg(QLatin1String(child->metaObject()->className()))
                                                           .arg(r.x() - panelRect.x())
                                                           .arg(r.y() - panelRect.y())
                                                           .arg(r.width())
                                                           .arg(r.height())));
        }

        // Nothing in the panel sits on top of anything else in it.
        const QList<QWidget *> direct = panel->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
        for (int i = 0; i < direct.size(); ++i)
            for (int j = i + 1; j < direct.size(); ++j)
                if (direct.at(i)->isVisible() && direct.at(j)->isVisible())
                    QVERIFY2(!direct.at(i)->geometry().intersects(direct.at(j)->geometry()),
                             "two parts of the colour panel overlap");

        // The docks themselves keep to their own space.
        const QList<QDockWidget *> docks = w.findChildren<QDockWidget *>();
        for (int i = 0; i < docks.size(); ++i)
            for (int j = i + 1; j < docks.size(); ++j) {
                QDockWidget *a = docks.at(i), *b = docks.at(j);
                if (!a->isVisible() || !b->isVisible() || a->visibleRegion().isEmpty()
                    || b->visibleRegion().isEmpty())
                    continue; // hidden, or a tab behind another
                QVERIFY2(!a->geometry().intersects(b->geometry()),
                         qPrintable(a->windowTitle() + QStringLiteral(" overlaps ") + b->windowTitle()));
            }
        QVERIFY(w.height() <= kSmallScreen.height()); // ... without the window outgrowing the screen
    }

    void aLongPaletteScrollsInsteadOfGrowingTheWindow()
    {
        MainWindow w;
        showAt(w, kSmallScreen);
        auto *panel = w.findChild<ColorPanel *>();
        QVERIFY(panel);
        for (int i = 0; i < 200; ++i)
            panel->addToPalette(QColor(i, 255 - i, (i * 7) % 256));
        QTest::qWait(500); // the docks slide to their new sizes
        QCoreApplication::processEvents();
        saveShot(w, "small-long-palette");
        const QSize min = w.minimumSizeHint();
        QVERIFY2(min.height() <= kSmallScreen.height(), qPrintable(QString::number(min.height())));
        QCOMPARE(w.size(), kSmallScreen);

        auto *scroll = w.findChild<QScrollArea *>(QStringLiteral("ColorScroll"));
        QVERIFY(scroll);
        QVERIFY(scroll->verticalScrollBar()->isVisible());
        QVERIFY(!scroll->horizontalScrollBar()->isVisible());
        QVERIFY(panel->width() <= scroll->viewport()->width()); // nothing lost off the side

        // The dock made room without running into its neighbours or its own tabs.
        auto *color = w.findChild<QDockWidget *>(QStringLiteral("ColorDock"));
        auto *history = w.findChild<QDockWidget *>(QStringLiteral("HistoryDock"));
        QVERIFY(color && history);
        QVERIFY(!color->geometry().intersects(history->geometry()));
        for (QTabBar *tabs : w.findChildren<QTabBar *>())
            if (tabs->isVisible() && tabs->parentWidget() == &w)
                QVERIFY2(!tabs->geometry().intersects(history->geometry()), "History covers the dock tabs");
    }

    void theWindowFitsASmallScreenWithEveryTool()
    {
        MainWindow w;
        showAt(w, kSmallScreen);
        auto *tools = w.findChild<QToolBar *>(QStringLiteral("ToolsBar"));
        QVERIFY(tools);
        for (QAction *tool : tools->actions()) {
            if (tool->isSeparator() || !tool->isEnabled())
                continue;
            tool->trigger();
            QTest::qWait(20);
            QCoreApplication::processEvents();
            saveShot(w, qPrintable(QStringLiteral("small-tool-") + tool->text()));
            // Well inside the screen, not a pixel under it: fonts and themes
            // differ from one machine to the next.
            const QSize min = w.minimumSizeHint();
            QVERIFY2(min.width() <= kNarrowest && min.height() <= kSmallScreen.height(),
                     qPrintable(QStringLiteral("%1: the window needs %2 x %3")
                                    .arg(tool->text())
                                    .arg(min.width())
                                    .arg(min.height())));
            // Whichever options bar the tool shows, nothing on it is cut short.
            for (QToolBar *bar : w.findChildren<QToolBar *>()) {
                if (!bar->isVisible() || !bar->objectName().endsWith(QStringLiteral("OptionsBar")))
                    continue;
                const QStringList cut = cutShort<QLabel>(bar) + cutShort<QAbstractButton>(bar);
                QVERIFY2(cut.isEmpty(), qPrintable(tool->text() + QStringLiteral(" cut short: ")
                                                   + cut.join(QStringLiteral(", "))));
            }
            QCOMPARE(w.size(), kSmallScreen);
        }
    }

    // The machine it's used on won't have this one's font. A bigger one makes
    // every row of every panel taller; the window must still fit.
    void theDocksKeepToThemselvesWithABiggerFont()
    {
        const QFont usual = QApplication::font();
        QFont big = usual;
        big.setPointSizeF(usual.pointSizeF() * 1.35);
        QApplication::setFont(big);
        {
            MainWindow w;
            showAt(w, kSmallScreen);
            auto *panel = w.findChild<ColorPanel *>();
            QVERIFY(panel);
            for (int i = 0; i < ColorPanel::MaxRecent; ++i)
                panel->noteUsed(QColor::fromHsv(i * 20, 255, 255));
            QTest::qWait(500);
            QCoreApplication::processEvents();
            saveShot(w, "small-big-font");

            const QSize min = w.minimumSizeHint();
            QVERIFY2(min.height() <= kSmallScreen.height() && min.width() <= kSmallScreen.width(),
                     qPrintable(QStringLiteral("the window needs %1 x %2").arg(min.width()).arg(min.height())));
            QCOMPARE(w.size(), kSmallScreen);

            QList<QWidget *> parts;
            for (QDockWidget *dock : w.findChildren<QDockWidget *>())
                if (dock->isVisible() && !dock->visibleRegion().isEmpty())
                    parts << dock;
            for (QTabBar *tabs : w.findChildren<QTabBar *>())
                if (tabs->isVisible() && tabs->parentWidget() == &w)
                    parts << tabs;
            QVERIFY(parts.size() >= 4);
            for (int i = 0; i < parts.size(); ++i)
                for (int j = i + 1; j < parts.size(); ++j)
                    QVERIFY2(!parts.at(i)->geometry().intersects(parts.at(j)->geometry()),
                             qPrintable(QStringLiteral("%1 overlaps %2")
                                            .arg(parts.at(i)->windowTitle().isEmpty()
                                                     ? QStringLiteral("the dock tabs")
                                                     : parts.at(i)->windowTitle(),
                                                 parts.at(j)->windowTitle().isEmpty()
                                                     ? QStringLiteral("the dock tabs")
                                                     : parts.at(j)->windowTitle())));
            // Every dock's panel is inside its dock.
            for (QDockWidget *dock : w.findChildren<QDockWidget *>())
                if (dock->isVisible() && dock->widget() && !dock->visibleRegion().isEmpty())
                    QVERIFY2(dock->rect().contains(dock->widget()->geometry()), qPrintable(dock->windowTitle()));
        }
        QApplication::setFont(usual);
    }

    void brushOptionsAreNotCutShort()
    {
        MainWindow w;
        showAt(w, kSmallScreen);
        saveShot(w, "small-brush");
        auto *bar = w.findChild<QToolBar *>(QStringLiteral("ToolOptionsBar"));
        QVERIFY(bar && bar->isVisible());
        QVERIFY(w.width() <= kSmallScreen.width()); // the bar must not force the window wider

        const QStringList cut = cutShort<QLabel>(bar) + cutShort<QCheckBox>(bar) + cutShort<QAbstractButton>(bar);
        QVERIFY2(cut.isEmpty(), qPrintable(QStringLiteral("cut short: ") + cut.join(QStringLiteral(", "))));

        // What is used all the time stays on the bar; the rest is one press away.
        for (QAbstractSlider *slider : bar->findChildren<QAbstractSlider *>())
            if (slider->isVisible())
                QVERIFY(slider->width() >= BrushOptionsBar::MinSliderWidth);
        for (const char *name : {"brushPressureSize", "brushPressureOpacity"}) {
            auto *widget = bar->findChild<QWidget *>(QLatin1String(name));
            QVERIFY2(widget && widget->isVisible(), name);
        }
        auto *overflow = bar->findChild<QWidget *>(QStringLiteral("qt_toolbar_ext_button"));
        auto *more = bar->findChild<QWidget *>(QStringLiteral("brushMore"));
        QVERIFY(overflow && more);
        QVERIFY(more->isVisible() || overflow->isVisible());
        if (!more->isVisible()) {
            QTest::mouseClick(overflow, Qt::LeftButton);
            QTRY_VERIFY(more->isVisible());
            saveShot(w, "small-brush-expanded");
            const QStringList stillCut = cutShort<QLabel>(bar) + cutShort<QCheckBox>(bar);
            QVERIFY2(stillCut.isEmpty(), qPrintable(stillCut.join(QStringLiteral(", "))));
        }

        // Both pressure switches say which one they are.
        QStringList pressure;
        for (QCheckBox *box : bar->findChildren<QCheckBox *>())
            if (box->text().contains(QStringLiteral("ressure"), Qt::CaseInsensitive))
                pressure << box->text();
        QCOMPARE(pressure.size(), 2);
        QVERIFY(pressure.at(0) != pressure.at(1));
    }

    void brushOptionsFitAWideWindowInFull()
    {
        MainWindow w;
        showAt(w, QSize(1900, 1000));
        saveShot(w, "wide-brush");
        auto *bar = w.findChild<QToolBar *>(QStringLiteral("ToolOptionsBar"));
        QVERIFY(bar);
        for (const char *name : {"brushMirror", "brushMore"}) {
            auto *widget = bar->findChild<QWidget *>(QLatin1String(name));
            QVERIFY2(widget && widget->isVisible(), name);
        }
        // Room to spare goes to the end of the bar, not into the controls.
        for (QAbstractSlider *slider : bar->findChildren<QAbstractSlider *>())
            if (slider->isVisible())
                QVERIFY(slider->width() <= BrushOptionsBar::SliderWidth);
        for (QSpinBox *spin : bar->findChildren<QSpinBox *>())
            if (spin->isVisible())
                QVERIFY2(spin->width() <= spin->sizeHint().width(), "a number box was stretched");
    }
};

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
