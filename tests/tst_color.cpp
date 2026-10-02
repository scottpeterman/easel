#include "color.h"

#include <QTest>

using namespace easeletch;

class TestColor : public QObject
{
    Q_OBJECT

private slots:
    void srgbRoundTripsAllBytes()
    {
        for (int i = 0; i < 256; ++i) {
            const float lin = srgb8ToLinear(quint8(i));
            QCOMPARE(int(linearToSrgb8(lin)), i);
        }
    }

    void transferCurveEndpoints()
    {
        QCOMPARE(srgbToLinear(0.0f), 0.0f);
        QCOMPARE(srgbToLinear(1.0f), 1.0f);
        QCOMPARE(linearToSrgb(0.0f), 0.0f);
        QVERIFY(qAbs(linearToSrgb(1.0f) - 1.0f) < 1e-6f);
        // Mid grey 128/255 is about 0.2158 in linear light.
        QVERIFY(qAbs(srgb8ToLinear(128) - 0.2158f) < 0.001f);
    }

    void colorPixelRoundTrip()
    {
        const QColor in(200, 100, 50, 255);
        const QColor out = pixelToColor(pixelFromColor(in));
        QVERIFY(qAbs(out.red() - in.red()) <= 1);
        QVERIFY(qAbs(out.green() - in.green()) <= 1);
        QVERIFY(qAbs(out.blue() - in.blue()) <= 1);
        QCOMPARE(out.alpha(), 255);
    }

    void pixelIsPremultiplied()
    {
        const Pixel p = pixelFromColor(QColor(255, 255, 255, 128));
        QVERIFY(qAbs(float(p.a) - 128.0f / 255.0f) < 0.002f);
        QVERIFY(qAbs(float(p.r) - float(p.a)) < 0.002f);
    }

    void displayOpaqueRed()
    {
        QCOMPARE(pixelToDisplay(pixelFromColor(Qt::red)), qRgba(255, 0, 0, 255));
        QCOMPARE(pixelToDisplay(pixelFromColor(QColor(0, 0, 0, 0))), QRgb(0));
    }
};

QTEST_GUILESS_MAIN(TestColor)
#include "tst_color.moc"
