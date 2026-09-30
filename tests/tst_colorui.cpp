#include "colorpanel.h"
#include "colorwheel.h"

#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

} // namespace

class TestColorUi : public QObject
{
    Q_OBJECT

private slots:
    void wheelSquareSetsSaturationAndValue()
    {
        ColorWheel w;
        w.resize(220, 220);
        w.setColor(QColor::fromHsvF(0.6f, 0.5f, 0.5f));
        QSignalSpy spy(&w, &ColorWheel::colorChanged);

        const QRectF sq = w.squareRect();
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, sq.topRight().toPoint() + QPoint(-1, 1));
        QCOMPARE(spy.count(), 1);
        QVERIFY(w.saturation() > 0.97 && w.value() > 0.97);
        QVERIFY(qAbs(w.hue() - 0.6) < 1e-3); // hue untouched

        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, sq.bottomLeft().toPoint() + QPoint(1, -1));
        QVERIFY(w.saturation() < 0.03 && w.value() < 0.03);
    }

    void wheelRingSetsHue()
    {
        ColorWheel w;
        w.resize(220, 220);
        w.setColor(QColor::fromHsvF(0.5f, 1.0f, 1.0f));
        const double mid = (w.outerRadius() + w.innerRadius()) / 2.0;

        // Right of centre is red (hue 0); top is hue 0.25 (counter-clockwise).
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, (w.center() + QPointF(mid, 0)).toPoint());
        QVERIFY(w.hue() < 0.01 || w.hue() > 0.99);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, (w.center() + QPointF(0, -mid)).toPoint());
        QVERIFY(qAbs(w.hue() - 0.25) < 0.01);
    }

    void wheelKeepsHueForGreys()
    {
        ColorWheel w;
        w.setColor(QColor::fromHsvF(0.33f, 1.0f, 1.0f));
        w.setColor(QColor(128, 128, 128)); // grey reports no hue
        QVERIFY(qAbs(w.hue() - 0.33) < 1e-3);
        QVERIFY(w.saturation() < 1e-6);
    }

    void panelRecentsAreDedupedAndCapped()
    {
        ColorPanel p;
        for (int i = 0; i < 20; ++i)
            p.noteUsed(QColor(i * 10, 0, 0));
        p.noteUsed(QColor(50, 0, 0)); // already there: moves to the front
        QCOMPARE(p.recentColors().size(), ColorPanel::MaxRecent);
        QCOMPARE(p.recentColors().first(), QColor(50, 0, 0));
        QCOMPARE(p.recentColors().count(QColor(50, 0, 0)), 1);
    }

    void panelColorsAreOpaqueAndHexWorks()
    {
        ColorPanel p;
        QSignalSpy spy(&p, &ColorPanel::colorChanged);
        p.setColor(QColor(10, 20, 30, 40));
        QCOMPARE(p.color(), QColor(10, 20, 30));
        QCOMPARE(spy.count(), 1);

        auto *hex = p.findChild<QLineEdit *>();
        QVERIFY(hex);
        hex->setText(QStringLiteral("1e88e5"));
        emit hex->editingFinished();
        QCOMPARE(p.color(), QColor(0x1e, 0x88, 0xe5));
        QCOMPARE(hex->text(), QStringLiteral("#1E88E5"));

        // The wheel follows colour set from outside.
        auto *wheel = p.findChild<ColorWheel *>();
        QVERIFY(near(wheel->color(), QColor(0x1e, 0x88, 0xe5)));
    }

    void paletteEditsPersist()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("s.ini"));
        {
            ColorPanel p;
            p.setSavedColors({Qt::red, Qt::green});
            p.addToPalette(Qt::blue);
            p.addToPalette(Qt::red);               // already present
            p.replaceInPalette(1, QColor(1, 2, 3)); // green -> custom
            p.removeFromPalette(0);                 // drop red
            p.noteUsed(Qt::yellow);
            p.setColor(QColor(9, 9, 9));
            QSettings s(ini, QSettings::IniFormat);
            p.saveSettings(s);
        }
        ColorPanel q;
        QSettings s(ini, QSettings::IniFormat);
        q.loadSettings(s);
        QCOMPARE(q.savedColors(), (QList<QColor>{QColor(1, 2, 3), QColor(Qt::blue)}));
        QCOMPARE(q.recentColors(), (QList<QColor>{QColor(Qt::yellow)}));
        QCOMPARE(q.color(), QColor(9, 9, 9));
    }
};

QTEST_MAIN(TestColorUi)
#include "tst_colorui.moc"
