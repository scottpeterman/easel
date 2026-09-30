#include "canvasview.h"

#include <QTest>

using namespace easel;

namespace {

// Reads a pixel from a GPU readback, mapping canvas coordinates through the view.
QColor pixelAtCanvas(const CanvasView &view, const QImage &shot, const QPointF &canvasPos)
{
    const qreal dpr = qreal(shot.width()) / qreal(view.width());
    const QPointF v = view.canvasToView().map(canvasPos) * dpr;
    return shot.pixelColor(v.toPoint());
}

bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

// Renders until the planner reports every visible tile at full resolution.
QImage settledFrame(CanvasView &view)
{
    QImage shot;
    for (int i = 0; i < 50; ++i) {
        shot = view.grabFramebuffer();
        if (shot.isNull() || view.lastFrameStats().fallbacks == 0)
            break;
    }
    return shot;
}

} // namespace

class TestCanvasView : public QObject
{
    Q_OBJECT

private slots:
    // --- View math: no GPU needed. ---

    void fitCentersCanvas()
    {
        TileStore store;
        CanvasView view;
        view.resize(800, 400);
        view.setDocument(&store, QSize(1000, 1000));

        const QPointF centre = view.canvasToView().map(QPointF(500, 500));
        QVERIFY(qAbs(centre.x() - 400.0) < 0.5);
        QVERIFY(qAbs(centre.y() - 200.0) < 0.5);
        QVERIFY(view.zoom() < 0.4 && view.zoom() > 0.3);
    }

    void fitFollowsResizeUntilUserNavigates()
    {
        TileStore store;
        CanvasView view;
        view.resize(400, 300);
        view.show(); // hidden widgets get no resize events
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(2000, 1500));
        QVERIFY(view.isAutoFit());
        const double small = view.zoom();

        view.resize(1200, 900); // e.g. the window reaching its real size
        QVERIFY(view.zoom() > small * 2.5);
        const QRectF b = view.canvasViewBounds();
        QVERIFY(b.left() >= 0 && b.top() >= 0 && b.right() <= 1200 && b.bottom() <= 900);

        view.zoomIn();
        QVERIFY(!view.isAutoFit());
        const double chosen = view.zoom();
        view.resize(600, 450);
        QCOMPARE(view.zoom(), chosen); // your zoom survives a resize

        view.fitToWindow(); // Fit turns it back on
        QVERIFY(view.isAutoFit());
        view.setPan(QPointF(10, 0));
        QVERIFY(!view.isAutoFit());
    }

    void zoomKeepsAnchorFixed()
    {
        TileStore store;
        CanvasView view;
        view.resize(600, 600);
        view.setDocument(&store, QSize(3000, 2000));

        const QPointF anchor(123, 456);
        const QPointF before = view.viewToCanvas(anchor);
        view.zoomBy(3.0, anchor);
        const QPointF after = view.viewToCanvas(anchor);
        QVERIFY(qAbs(before.x() - after.x()) < 1e-6);
        QVERIFY(qAbs(before.y() - after.y()) < 1e-6);
    }

    void rotationPivotsOnViewCentre()
    {
        TileStore store;
        CanvasView view;
        view.resize(500, 500);
        view.setDocument(&store, QSize(1000, 1000));

        const QPointF viewCentre(250, 250);
        const QPointF canvasAtCentre = view.viewToCanvas(viewCentre);
        view.rotateBy(30.0);
        view.rotateBy(-75.0);
        QCOMPARE(view.rotation(), -45.0);
        const QPointF after = view.viewToCanvas(viewCentre);
        QVERIFY(qAbs(after.x() - canvasAtCentre.x()) < 1e-6);
        QVERIFY(qAbs(after.y() - canvasAtCentre.y()) < 1e-6);

        view.resetRotation();
        QCOMPARE(view.rotation(), 0.0);
    }

    // --- GPU rendering: skipped when no QRhi backend can start. ---

    void rendersTilesInsideCanvas()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(0, 0, 100, 100), Qt::red);

        CanvasView view;
        view.resize(400, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(200, 200));

        const QImage shot = settledFrame(view);
        if (shot.isNull())
            QSKIP("No GPU backend available for QRhiWidget");

        QVERIFY(near(pixelAtCanvas(view, shot, {50, 50}), Qt::red));
        QVERIFY(near(pixelAtCanvas(view, shot, {150, 150}), Qt::white));
        QVERIFY(near(shot.pixelColor(2, 2), QColor(0x3a, 0x3a, 0x3a)));
    }

    void transparentShowsChecker()
    {
        TileStore store; // transparent default
        CanvasView view;
        view.resize(300, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(256, 256));
        view.setZoomCentered(1.0);

        const QImage shot = settledFrame(view);
        if (shot.isNull())
            QSKIP("No GPU backend available for QRhiWidget");

        // Adjacent checker cells differ: one is white, one is 0xCC.
        const QColor a = pixelAtCanvas(view, shot, {100.5, 100.5});
        const QColor b = pixelAtCanvas(view, shot, {108.5, 100.5});
        QVERIFY(near(a, Qt::white) || near(a, QColor(0xcc, 0xcc, 0xcc)));
        QVERIFY(near(b, Qt::white) || near(b, QColor(0xcc, 0xcc, 0xcc)));
        QVERIFY(a != b);
    }

    void refreshShowsEdits()
    {
        TileStore store(Qt::white);
        CanvasView view;
        view.resize(300, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(64, 64));
        view.setZoomCentered(1.0);

        store.fillRect(QRect(0, 0, 64, 64), Qt::blue);
        view.refresh();
        QImage shot = settledFrame(view);
        if (shot.isNull())
            QSKIP("No GPU backend available for QRhiWidget");
        QVERIFY(near(pixelAtCanvas(view, shot, {32, 32}), Qt::blue));

        store.fillRect(QRect(0, 0, 64, 64), Qt::green);
        view.refresh();
        shot = settledFrame(view);
        QVERIFY(near(pixelAtCanvas(view, shot, {32, 32}), Qt::green));
    }

    void zoomedOutAveragesInLinearLight()
    {
        // 1-px black/white stripes average to linear 0.5 = sRGB 188, not 128.
        TileStore store;
        QImage stripes(512, 512, QImage::Format_RGB32);
        for (int x = 0; x < 512; ++x)
            for (int y = 0; y < 512; ++y)
                stripes.setPixel(x, y, (x & 1) ? 0xffffffff : 0xff000000);
        store.writeImage(stripes);

        CanvasView view;
        view.resize(300, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(512, 512));
        view.setZoomCentered(0.25);

        const QImage shot = settledFrame(view);
        if (shot.isNull())
            QSKIP("No GPU backend available for QRhiWidget");
        QVERIFY(view.lastFrameStats().level >= 2);
        const QColor c = pixelAtCanvas(view, shot, {256, 256});
        QVERIFY2(qAbs(c.red() - 188) <= 3, qPrintable(QString::number(c.red())));
    }

    void rotatedViewStillCoversCanvas()
    {
        TileStore store(Qt::white);
        store.fillRect(QRect(0, 0, 400, 400), Qt::red);

        CanvasView view;
        view.resize(500, 500);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setDocument(&store, QSize(400, 400));
        view.rotateBy(37.0);
        view.fitToWindow(); // fit accounts for rotation: all four corners on screen

        const QImage shot = settledFrame(view);
        if (shot.isNull())
            QSKIP("No GPU backend available for QRhiWidget");
        for (const QPointF p : {QPointF(5, 5), QPointF(395, 5), QPointF(200, 200), QPointF(5, 395), QPointF(395, 395)})
            QVERIFY2(near(pixelAtCanvas(view, shot, p), Qt::red),
                     qPrintable(QStringLiteral("%1,%2").arg(p.x()).arg(p.y())));
    }
};

QTEST_MAIN(TestCanvasView)
#include "tst_canvasview.moc"
