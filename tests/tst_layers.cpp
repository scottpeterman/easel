#include "history.h"
#include "layerstack.h"

#include <QTest>

#include <cmath>

using namespace easel;

namespace {

// One opaque pixel blended over another, both given and returned as 8-bit sRGB.
QColor blend(const QColor &backdrop, const QColor &source, BlendMode mode, float opacity = 1.0f)
{
    const Pixel b = pixelFromColor(backdrop), s = pixelFromColor(source);
    float dst[4] = {float(b.r), float(b.g), float(b.b), float(b.a)};
    const float src[4] = {float(s.r), float(s.g), float(s.b), float(s.a)};
    blendPixels(dst, src, 1, opacity, mode);
    return pixelToColor(makePixel(dst[0], dst[1], dst[2], dst[3]));
}

bool near(const QColor &a, const QColor &b, int tol = 1)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol && qAbs(a.alpha() - b.alpha()) <= tol;
}

QColor at(const TileStore &s, int x, int y)
{
    return pixelToColor(s.pixel(x, y));
}

Layer raster(const QString &name, const QColor &fill = QColor(), const QRect &rect = QRect())
{
    Layer l;
    l.name = name;
    if (fill.isValid())
        l.store.fillRect(rect, fill);
    return l;
}

Layer group(const QString &name)
{
    Layer l;
    l.name = name;
    l.group = true;
    return l;
}

// White background (as a default pixel, no tiles) under a red square and a
// blue square that overlap in (40..60, 40..60).
struct Doc {
    LayerStack stack;
    int bg, red, blue;

    Doc()
    {
        stack.setSize(QSize(200, 200));
        Layer b;
        b.name = QStringLiteral("Background");
        b.store = TileStore(Qt::white);
        bg = stack.insert(std::move(b), 0, 0);
        red = stack.insert(raster(QStringLiteral("Red"), Qt::red, QRect(20, 20, 40, 40)), 0, 1);
        blue = stack.insert(raster(QStringLiteral("Blue"), Qt::blue, QRect(40, 40, 40, 40)), 0, 2);
        stack.recompositeAll();
    }
};

} // namespace

class TestLayers : public QObject
{
    Q_OBJECT

private slots:
    // --- Blend modes ---

    void normalIsSourceOver()
    {
        QCOMPARE(blend(Qt::white, Qt::red, BlendMode::Normal), QColor(Qt::red));
        // Half of red over white, mixed in linear light: 0.5 linear is 188 in sRGB.
        QVERIFY(near(blend(Qt::white, Qt::red, BlendMode::Normal, 0.5f), QColor(255, 188, 188)));
    }

    void blendModesHaveTheirNeutralColours()
    {
        const QColor base(200, 120, 40);
        QVERIFY(near(blend(base, Qt::white, BlendMode::Multiply), base));
        QVERIFY(near(blend(base, Qt::black, BlendMode::Screen), base));
        QVERIFY(near(blend(base, QColor(128, 128, 128), BlendMode::Overlay), base, 2));
        QVERIFY(near(blend(base, QColor(128, 128, 128), BlendMode::SoftLight), base, 2));
        QVERIFY(near(blend(base, Qt::white, BlendMode::Darken), base));
        QVERIFY(near(blend(base, Qt::black, BlendMode::Lighten), base));
        QVERIFY(near(blend(base, Qt::black, BlendMode::ColorDodge), base));
        QVERIFY(near(blend(base, Qt::white, BlendMode::ColorBurn), base));
        QVERIFY(near(blend(base, Qt::black, BlendMode::Difference), base));
        QVERIFY(near(blend(base, base, BlendMode::Hue), base, 2));
        QVERIFY(near(blend(base, base, BlendMode::Color), base, 2));
    }

    void blendModesMatchTheUsualNumbers()
    {
        // Worked on sRGB values, as in other editors.
        QVERIFY(near(blend(QColor(128, 128, 128), QColor(128, 128, 128), BlendMode::Multiply), QColor(64, 64, 64)));
        QVERIFY(near(blend(QColor(128, 128, 128), QColor(128, 128, 128), BlendMode::Screen), QColor(192, 192, 192)));
        QVERIFY(near(blend(QColor(200, 100, 50), QColor(50, 150, 250), BlendMode::Difference), QColor(150, 50, 200)));
        QVERIFY(near(blend(QColor(200, 100, 50), QColor(50, 150, 250), BlendMode::Darken), QColor(50, 100, 50)));
        QVERIFY(near(blend(QColor(200, 100, 50), QColor(50, 150, 250), BlendMode::Lighten), QColor(200, 150, 250)));
        QVERIFY(near(blend(QColor(64, 64, 64), QColor(64, 64, 64), BlendMode::Overlay), QColor(32, 32, 32)));
        QVERIFY(near(blend(QColor(100, 100, 100), QColor(128, 128, 128), BlendMode::ColorDodge),
                     QColor(201, 201, 201), 2));
        // Color keeps the backdrop's lightness: red over mid grey is not pure red.
        const QColor c = blend(QColor(128, 128, 128), Qt::red, BlendMode::Color);
        QVERIFY(c.red() > c.green() && c.green() == c.blue());
        QVERIFY(qAbs(0.3 * c.red() + 0.59 * c.green() + 0.11 * c.blue() - 128.0) < 3.0);
        // Hue of a grey source has no hue to give: the result is grey.
        const QColor h = blend(QColor(200, 100, 50), QColor(128, 128, 128), BlendMode::Hue);
        QVERIFY(qAbs(h.red() - h.green()) <= 1 && qAbs(h.green() - h.blue()) <= 1);
    }

    void blendOverTransparentIsJustTheSource()
    {
        float dst[4] = {0, 0, 0, 0};
        const Pixel s = pixelFromColor(QColor(10, 200, 90));
        const float src[4] = {float(s.r), float(s.g), float(s.b), float(s.a)};
        blendPixels(dst, src, 1, 1.0f, BlendMode::Multiply);
        QCOMPARE(pixelToColor(makePixel(dst[0], dst[1], dst[2], dst[3])).rgba(), QColor(10, 200, 90).rgba());
    }

    void blendKeysRoundTrip()
    {
        for (int i = 0; i < BlendModeCount; ++i) {
            bool ok = false;
            QCOMPARE(blendModeFromKey(blendModeKey(BlendMode(i)), &ok), BlendMode(i));
            QVERIFY(ok);
        }
        bool ok = true;
        QCOMPARE(blendModeFromKey(QStringLiteral("nonsense"), &ok), BlendMode::Normal);
        QVERIFY(!ok);
    }

    // --- Compositing ---

    void compositesBottomToTop()
    {
        Doc d;
        const TileStore &c = d.stack.composite();
        QCOMPARE(at(c, 5, 5), QColor(Qt::white));   // background default
        QCOMPARE(at(c, 30, 30), QColor(Qt::red));
        QCOMPARE(at(c, 50, 50), QColor(Qt::blue));  // blue is on top
        QCOMPARE(at(c, 70, 70), QColor(Qt::blue));
        QCOMPARE(at(c, 150, 150), QColor(Qt::white)); // no tile anywhere: the default
        QVERIFY(!c.hasTile(TileStore::tileAt(150, 150)));
    }

    void singleLayerSharesItsTiles()
    {
        TileStore s;
        s.fillRect(QRect(0, 0, 100, 100), Qt::red);
        const LayerStack stack = LayerStack::single(s, QSize(100, 100), QStringLiteral("Background"));
        QCOMPARE(stack.composite().tileCount(), s.tileCount());
        for (const TileCoord c : s.tileCoords())
            QCOMPARE(stack.composite().tile(c).cacheKey(), stack.layers().first().store.tile(c).cacheKey());
        QVERIFY(!stack.composite().hasDirty());
    }

    void visibilityOpacityAndBlendApply()
    {
        Doc d;
        d.stack.layer(d.blue)->visible = false;
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::red));
        QCOMPARE(at(d.stack.composite(), 70, 70), QColor(Qt::white));

        d.stack.layer(d.blue)->visible = true;
        d.stack.layer(d.blue)->opacity = 0.5;
        d.stack.recompositeAll();
        QVERIFY(near(at(d.stack.composite(), 70, 70), QColor(188, 188, 255))); // half blue over white
        QVERIFY(near(at(d.stack.composite(), 50, 50), QColor(188, 0, 188)));   // half blue over red

        d.stack.layer(d.blue)->opacity = 1.0;
        d.stack.layer(d.blue)->blend = BlendMode::Multiply;
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::black)); // blue x red
        QCOMPARE(at(d.stack.composite(), 70, 70), QColor(Qt::blue));  // blue x white
    }

    void hidingTheBackgroundShowsTransparency()
    {
        Doc d;
        d.stack.layer(d.bg)->visible = false;
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 150, 150).alpha(), 0);
        QCOMPARE(at(d.stack.composite(), 5, 5).alpha(), 0); // same tile as the red square
        QCOMPARE(at(d.stack.composite(), 30, 30), QColor(Qt::red));
    }

    void updateCompositeFollowsPainting()
    {
        Doc d;
        d.stack.compositeStore()->takeDirty();
        d.stack.layer(d.red)->store.fillRect(QRect(150, 150, 10, 10), Qt::green);
        QCOMPARE(at(d.stack.composite(), 155, 155), QColor(Qt::white)); // not yet
        d.stack.updateComposite();
        QCOMPARE(at(d.stack.composite(), 155, 155), QColor(Qt::green));
        const QSet<TileCoord> dirty = d.stack.compositeStore()->takeDirty();
        QCOMPARE(dirty, QSet<TileCoord>{TileStore::tileAt(155, 155)});
        // Painting under an opaque layer changes nothing on top.
        d.stack.layer(d.red)->store.fillRect(QRect(65, 65, 5, 5), Qt::green);
        d.stack.updateComposite();
        QCOMPARE(at(d.stack.composite(), 66, 66), QColor(Qt::blue));
    }

    void groupsCompositeOnTheirOwnFirst()
    {
        Doc d;
        const int g = d.stack.insert(group(QStringLiteral("Group")), 0, 1);
        QVERIFY(d.stack.move(d.red, g, 0));
        QVERIFY(d.stack.move(d.blue, g, 1));
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::blue));

        // At half opacity the group fades as one picture: where blue covers
        // red, no red shows through.
        d.stack.layer(g)->opacity = 0.5;
        d.stack.recompositeAll();
        QVERIFY(near(at(d.stack.composite(), 50, 50), QColor(188, 188, 255)));
        QVERIFY(near(at(d.stack.composite(), 30, 30), QColor(255, 188, 188)));

        d.stack.layer(g)->visible = false;
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::white));
        QVERIFY(!d.stack.isShown(d.red));
        d.stack.layer(g)->visible = true;
        d.stack.layer(g)->locked = true;
        QVERIFY(d.stack.isLocked(d.blue));
        QVERIFY(!d.stack.isLocked(d.bg));
    }

    // --- Structure ---

    void insertMoveAndRemoveKeepOrder()
    {
        Doc d;
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg, d.red, d.blue}));
        QVERIFY(d.stack.move(d.blue, 0, 0));
        QCOMPARE(d.stack.children(0), (QList<int>{d.blue, d.bg, d.red}));
        QVERIFY(d.stack.move(d.blue, 0, 99));
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg, d.red, d.blue}));

        const int g = d.stack.insert(group(QStringLiteral("G")), 0, 1);
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg, g, d.red, d.blue}));
        QVERIFY(d.stack.move(d.red, g, 0));
        QVERIFY(d.stack.move(d.blue, g, 0));
        QCOMPARE(d.stack.children(g), (QList<int>{d.blue, d.red}));
        QVERIFY(d.stack.isInside(d.red, g));
        QCOMPARE(d.stack.subtree(g).size(), 3);

        // A group can't go inside itself, and only groups hold layers.
        const int inner = d.stack.insert(group(QStringLiteral("Inner")), g, 0);
        QVERIFY(!d.stack.move(g, inner, 0));
        QVERIFY(!d.stack.move(g, g, 0));
        QVERIFY(!d.stack.move(d.bg, d.red, 0));

        d.stack.setActive(d.red);
        d.stack.remove(g);
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg}));
        QCOMPARE(d.stack.activeId(), d.bg);
    }

    void rearrangeChecksTheOrder()
    {
        Doc d;
        const int g = d.stack.insert(group(QStringLiteral("G")), 0, 3);
        QVERIFY(d.stack.rearrange({{d.blue, 0}, {d.bg, 0}, {g, 0}, {d.red, g}}));
        QCOMPARE(d.stack.children(0), (QList<int>{d.blue, d.bg, g}));
        QCOMPARE(d.stack.children(g), (QList<int>{d.red}));
        QVERIFY(!d.stack.rearrange({{d.blue, 0}, {d.bg, 0}, {g, 0}}));               // one missing
        QVERIFY(!d.stack.rearrange({{d.blue, 0}, {d.bg, 0}, {g, 0}, {d.blue, 0}}));  // one twice
        QVERIFY(!d.stack.rearrange({{d.blue, d.bg}, {d.bg, 0}, {g, 0}, {d.red, g}})); // parent isn't a group
        QVERIFY(!d.stack.rearrange({{d.blue, 0}, {d.bg, 0}, {g, g}, {d.red, g}}));   // inside itself
        QCOMPARE(d.stack.children(0), (QList<int>{d.blue, d.bg, g}));               // unchanged
    }

    void duplicateCopiesAGroupWithItsContents()
    {
        Doc d;
        const int g = d.stack.insert(group(QStringLiteral("G")), 0, 1);
        d.stack.move(d.red, g, 0);
        d.stack.move(d.blue, g, 1);
        const int copy = d.stack.duplicate(g);
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg, g, copy}));
        const QList<int> inner = d.stack.children(copy);
        QCOMPARE(inner.size(), 2);
        QCOMPARE(d.stack.layer(inner.at(0))->name, QStringLiteral("Red"));
        QCOMPARE(d.stack.layer(inner.at(1))->name, QStringLiteral("Blue"));
        QCOMPARE(d.stack.layer(copy)->name, QStringLiteral("G copy"));
        // The copies are independent of the originals.
        d.stack.layer(inner.at(0))->store.fillRect(QRect(20, 20, 5, 5), Qt::green);
        QCOMPARE(at(d.stack.layer(d.red)->store, 22, 22), QColor(Qt::red));
    }

    void mergingLooksTheSame()
    {
        Doc d;
        d.stack.layer(d.blue)->opacity = 0.6;
        d.stack.recompositeAll();
        const TileStore before = d.stack.composite();
        const QList<QPoint> probes{QPoint(5, 5), QPoint(30, 30), QPoint(50, 50), QPoint(70, 70), QPoint(150, 150)};

        QVERIFY(!d.stack.mergeDown(d.bg)); // nothing below
        QVERIFY(d.stack.mergeDown(d.blue));
        d.stack.recompositeAll();
        QCOMPARE(d.stack.count(), 2);
        QCOMPARE(d.stack.activeId(), d.red);
        for (const QPoint p : probes)
            QVERIFY2(near(at(d.stack.composite(), p.x(), p.y()), at(before, p.x(), p.y())), "after merge down");

        QVERIFY(d.stack.mergeDown(d.red)); // into the background, whose white is a default pixel
        d.stack.recompositeAll();
        QCOMPARE(d.stack.count(), 1);
        for (const QPoint p : probes)
            QVERIFY2(near(at(d.stack.composite(), p.x(), p.y()), at(before, p.x(), p.y())), "after second merge");
    }

    void mergingABlendModeKeepsItWhereTheLayersOverlap()
    {
        Doc d;
        d.stack.layer(d.blue)->blend = BlendMode::Screen;
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::magenta)); // red screened with blue
        QVERIFY(d.stack.mergeDown(d.blue));
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::magenta));
        QCOMPARE(at(d.stack.composite(), 30, 30), QColor(Qt::red));
    }

    void mergeGroupAndFlattenLookTheSame()
    {
        Doc d;
        const int g = d.stack.insert(group(QStringLiteral("G")), 0, 1);
        d.stack.move(d.red, g, 0);
        d.stack.move(d.blue, g, 1);
        d.stack.layer(g)->opacity = 0.5;
        d.stack.recompositeAll();
        const TileStore before = d.stack.composite();
        const QList<QPoint> probes{QPoint(5, 5), QPoint(30, 30), QPoint(50, 50), QPoint(70, 70), QPoint(150, 150)};

        QVERIFY(d.stack.mergeGroup(g));
        d.stack.recompositeAll();
        QCOMPARE(d.stack.count(), 2);
        QVERIFY(!d.stack.layer(g)->group);
        QCOMPARE(d.stack.layer(g)->opacity, 0.5);
        for (const QPoint p : probes)
            QVERIFY(near(at(d.stack.composite(), p.x(), p.y()), at(before, p.x(), p.y())));

        d.stack.flatten(QStringLiteral("Background"));
        d.stack.recompositeAll();
        QCOMPARE(d.stack.count(), 1);
        QCOMPARE(d.stack.active()->name, QStringLiteral("Background"));
        for (const QPoint p : probes)
            QVERIFY(near(at(d.stack.composite(), p.x(), p.y()), at(before, p.x(), p.y())));
    }

    // --- Undo ---

    void copiesLeaveTheOriginalInPlace()
    {
        Doc d;
        const Layer *before = d.stack.layer(d.red);
        const LayerStack snap = d.stack.snapshot();
        const LayerStack copy = d.stack;
        QCOMPARE(d.stack.layer(d.red), before); // tools hold pointers to these
        QVERIFY(snap.layer(d.red) != before);
        QVERIFY(copy.layer(d.red) != before);
        QCOMPARE(snap.composite().tileCount(), 0);
        QCOMPARE(copy.composite().tileCount(), d.stack.composite().tileCount());
    }

    void historyUndoesTilesPerLayerAndStackChanges()
    {
        Doc d;
        History h;
        h.reset(QStringLiteral("New"));

        // A stroke on the red layer.
        {
            const TileStore pre = d.stack.layer(d.red)->store;
            d.stack.layer(d.red)->store.fillRect(QRect(100, 100, 10, 10), Qt::green);
            QHash<TileCoord, QImage> tiles;
            const TileCoord c = TileStore::tileAt(100, 100);
            tiles.insert(c, pre.tile(c));
            h.push(QStringLiteral("Brush"), d.red, std::move(tiles));
        }
        // Delete the blue layer.
        {
            LayerStack before = d.stack.snapshot();
            d.stack.remove(d.blue);
            h.pushState(QStringLiteral("Delete Layer"), std::move(before), d.stack);
        }
        // Hide the red layer: costs nothing.
        const qint64 bytes = h.memoryBytes();
        {
            LayerStack before = d.stack.snapshot();
            d.stack.layer(d.red)->visible = false;
            h.pushState(QStringLiteral("Hide Layer"), std::move(before), d.stack);
        }
        QCOMPARE(h.memoryBytes(), bytes);
        QCOMPARE(h.count(), 3);

        QVERIFY(h.undo(d.stack)); // show again
        QVERIFY(d.stack.layer(d.red)->visible);
        QVERIFY(h.undo(d.stack)); // blue is back, on top, with its pixels
        QCOMPARE(d.stack.children(0), (QList<int>{d.bg, d.red, d.blue}));
        QCOMPARE(at(d.stack.layer(d.blue)->store, 70, 70), QColor(Qt::blue));
        QVERIFY(!h.undo(d.stack)); // the stroke: tiles only
        QCOMPARE(at(d.stack.layer(d.red)->store, 105, 105).alpha(), 0);

        QVERIFY(!h.redo(d.stack));
        QCOMPARE(at(d.stack.layer(d.red)->store, 105, 105), QColor(Qt::green));
        QVERIFY(h.jumpTo(3, d.stack));
        QCOMPARE(d.stack.count(), 2);
        QVERIFY(!d.stack.layer(d.red)->visible);
        QVERIFY(h.jumpTo(0, d.stack));
        QCOMPARE(d.stack.count(), 3);
        d.stack.recompositeAll();
        QCOMPARE(at(d.stack.composite(), 50, 50), QColor(Qt::blue));
        QCOMPARE(at(d.stack.composite(), 105, 105), QColor(Qt::white));
    }

    void deletedLayersCountAgainstTheUndoBudget()
    {
        Doc d;
        History h;
        LayerStack before = d.stack.snapshot();
        const qint64 blueBytes = d.stack.layer(d.blue)->store.memoryBytes();
        QVERIFY(blueBytes > 0);
        d.stack.remove(d.blue);
        h.pushState(QStringLiteral("Delete Layer"), std::move(before), d.stack);
        QCOMPARE(h.memoryBytes(), blueBytes);
        h.undo(d.stack);
        QCOMPARE(h.memoryBytes(), 0); // the entry now holds the state without it
    }

    void newIdsAreNeverReused()
    {
        Doc d;
        History h;
        LayerStack before = d.stack.snapshot();
        const int added = d.stack.insert(raster(QStringLiteral("New")), 0, 99);
        h.pushState(QStringLiteral("New Layer"), std::move(before), d.stack);
        h.undo(d.stack);
        const int next = d.stack.insert(raster(QStringLiteral("Other")), 0, 99);
        QVERIFY(next != added); // redo of the old entry is gone, but ids in flight stay unique
    }
};

QTEST_GUILESS_MAIN(TestLayers)
#include "tst_layers.moc"
