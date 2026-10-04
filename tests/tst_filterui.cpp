#include "canvasview.h"
#include "color.h"
#include "filterdialog.h"
#include "mainwindow.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSignalSpy>
#include <QTest>

using namespace easeletch;

namespace {

float redAt(MainWindow &w, int x, int y)
{
    return float(w.layer()->pixel(x, y).r);
}

// 400 x 300 black canvas, white right half: one hard edge at x = 200.
void setupWindow(MainWindow &w)
{
    w.resize(1200, 800);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.newDocument(QSize(400, 300), Qt::black);
    w.canvasView()->setZoomCentered(1.0);
    w.layer()->fillRect(QRect(200, 0, 200, 300), QColor(Qt::white));
    w.canvasView()->refresh();
}

Filter blur(double radius)
{
    Filter f = Filter::make(FilterType::GaussianBlur);
    f.radius = radius;
    return f;
}

} // namespace

class TestFilterUi : public QObject
{
    Q_OBJECT

private slots:
    void applyIsOneUndoStep()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.applyFilter(blur(5.0)));
        QVERIFY(!w.isPreviewingFilter());
        QCOMPARE(w.history().count(), 1);
        QCOMPARE(w.history().label(0), QStringLiteral("Gaussian Blur"));
        QVERIFY(redAt(w, 198, 100) > 0.2f);
        w.undo();
        QCOMPARE(redAt(w, 198, 100), 0.0f);
        w.redo();
        QVERIFY(redAt(w, 198, 100) > 0.2f);
    }

    void previewsStartFromTheOriginalEachTime()
    {
        MainWindow w;
        setupWindow(w);
        QVERIFY(w.previewFilter(blur(20.0)));
        QVERIFY(w.isPreviewingFilter());
        const float wide = redAt(w, 180, 100);
        QVERIFY(wide > 0.1f);
        QCOMPARE(w.history().count(), 0); // nothing recorded while trying

        // A smaller radius after a larger one: narrower, not blurred twice.
        QVERIFY(w.previewFilter(blur(2.0)));
        QCOMPARE(redAt(w, 180, 100), 0.0f);
        QVERIFY(redAt(w, 199, 100) > 0.3f);

        // Cancelled: exactly as it was.
        w.endFilterPreview(false);
        QVERIFY(!w.isPreviewingFilter());
        QCOMPARE(redAt(w, 199, 100), 0.0f);
        QCOMPARE(redAt(w, 200, 100), 1.0f);
        QCOMPARE(w.history().count(), 0);

        // Kept: one step, undone in one.
        w.previewFilter(blur(20.0));
        w.previewFilter(blur(3.0));
        w.endFilterPreview(true);
        QCOMPARE(w.history().count(), 1);
        QVERIFY(redAt(w, 199, 100) > 0.3f);
        w.undo();
        QCOMPARE(redAt(w, 199, 100), 0.0f);
    }

    void insideTheSelectionOnly()
    {
        MainWindow w;
        setupWindow(w);
        w.setSelection(Selection::rect(QRect(0, 0, 400, 100)));
        QVERIFY(w.applyFilter(blur(6.0)));
        QVERIFY(redAt(w, 197, 50) > 0.1f);
        QCOMPARE(redAt(w, 197, 200), 0.0f);
    }

    void repeatLastFilter()
    {
        MainWindow w;
        setupWindow(w);
        Filter f = Filter::make(FilterType::Pixelate);
        f.cell = 16;
        QVERIFY(w.applyFilter(f));
        const Pixel once = w.layer()->pixel(195, 100);
        QVERIFY(float(once.r) > 0.0f && float(once.r) < 1.0f); // the block across the edge is grey
        w.undo();
        w.repeatFilter();
        QCOMPARE(w.history().label(0), QStringLiteral("Pixelate"));
        QVERIFY(samePixel(w.layer()->pixel(195, 100), once));
    }

    void despeckleIsOneUndoStep()
    {
        MainWindow w;
        setupWindow(w);
        w.layer()->fillRect(QRect(300, 200, 2, 2), QColor(Qt::blue)); // a stray dot
        w.canvasView()->refresh();
        const Pixel paper = w.layer()->pixel(310, 210);
        const qsizetype steps = w.history().count();
        QVERIFY(w.applyFilter(Filter::make(FilterType::Despeckle)));
        QCOMPARE(w.history().count(), steps + 1);
        QCOMPARE(w.history().undoLabel(), QStringLiteral("Despeckle"));
        QVERIFY(samePixel(w.layer()->pixel(300, 200), paper));
        w.undo();
        QVERIFY(!samePixel(w.layer()->pixel(300, 200), paper));
        // The dialog shows this filter's two settings.
        FilterDialog dlg(Filter::make(FilterType::Despeckle));
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Despeckle"));
        QCOMPARE(dlg.filter().speck, 30);
    }

    void refusedWhereThereAreNoPixels()
    {
        MainWindow w;
        setupWindow(w);
        w.setLayerLocked(w.layers().activeId(), true);
        const qsizetype steps = w.history().count();
        QVERIFY(!w.applyFilter(blur(5.0)));
        QVERIFY(!w.previewFilter(blur(5.0)));
        QVERIFY(!w.isPreviewingFilter());
        QCOMPARE(w.history().count(), steps);
        w.setLayerLocked(w.layers().activeId(), false);
        QVERIFY(w.addAdjustmentLayer(AdjustmentType::Levels));
        QVERIFY(!w.applyFilter(blur(5.0)));
    }

    void dialogReportsSettingsAfterAPause()
    {
        FilterDialog dlg(Filter::make(FilterType::Sharpen));
        QSignalSpy preview(&dlg, &FilterDialog::previewRequested);
        const auto spins = dlg.findChildren<QDoubleSpinBox *>();
        QCOMPARE(spins.size(), 2); // amount and radius
        // Several quick changes: one preview, with the settings as they ended.
        for (QDoubleSpinBox *s : spins) {
            if (s->suffix() == QStringLiteral("%")) {
                s->setValue(150.0);
                s->setValue(250.0);
            } else {
                s->setValue(3.0);
            }
        }
        QCOMPARE(preview.count(), 0);
        QVERIFY(preview.wait(1000));
        QCOMPARE(preview.count(), 1);
        QCOMPARE(dlg.filter().amount, 2.5);
        QCOMPARE(dlg.filter().radius, 3.0);

        // Preview unticked: the canvas is asked to show the original, and changes stay quiet.
        QSignalSpy cleared(&dlg, &FilterDialog::previewCleared);
        QCheckBox *box = nullptr;
        for (QCheckBox *c : dlg.findChildren<QCheckBox *>())
            if (c->text() == QStringLiteral("Preview"))
                box = c;
        QVERIFY(box);
        box->setChecked(false);
        QCOMPARE(cleared.count(), 1);
        spins.first()->setValue(spins.first()->value() + 10.0);
        QTest::qWait(150);
        QCOMPARE(preview.count(), 1);
        box->setChecked(true);
        QCOMPARE(preview.count(), 2);
    }
};

QTEST_MAIN(TestFilterUi)
#include "tst_filterui.moc"
