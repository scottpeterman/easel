#include "shapes.h"

#include "regionops.h"

#include <QJsonArray>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace easeletch {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Larger than this and the image is no use on any canvas.
constexpr int kMaxSide = 16384;

QPen penFor(const ShapeSettings &s, double width)
{
    const QColor c(s.lineColor.red(), s.lineColor.green(), s.lineColor.blue());
    // Sharp corners on straight sides, as drawn with a ruler; round ones on a curve.
    QPen pen(c, width, Qt::SolidLine, s.closed ? Qt::SquareCap : Qt::RoundCap,
             s.curved ? Qt::RoundJoin : Qt::MiterJoin);
    pen.setMiterLimit(4.0);
    return pen;
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0.0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

// Adds a polygon to a path that's filled by winding, turning it the one way
// round every piece has to go for the pieces to add up, not cut holes.
void addPiece(QPainterPath &path, QPolygonF piece)
{
    double area = 0.0;
    for (int i = 0; i < piece.size(); ++i) {
        const QPointF a = piece.at(i), b = piece.at((i + 1) % piece.size());
        area += a.x() * b.y() - b.x() * a.y();
    }
    if (area == 0.0)
        return;
    if (area < 0.0)
        std::reverse(piece.begin(), piece.end());
    path.addPolygon(piece);
    path.closeSubpath();
}

// A tapered line as an area to fill: the line walked in short steps, a disc
// as wide as the line is there at each, and each pair of discs joined.
QPainterPath taperedLine(const ShapeSettings &s, const QPainterPath &line)
{
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    const QList<QPolygonF> runs = line.toSubpathPolygons();
    if (runs.isEmpty())
        return out;
    const QPolygonF &run = runs.first();
    double length = 0.0;
    for (int i = 1; i < run.size(); ++i)
        length += QLineF(run.at(i - 1), run.at(i)).length();
    if (length <= 0.0)
        return out;

    struct Step {
        QPointF pos;
        double radius;
    };
    QList<Step> steps;
    constexpr double stride = 3.0; // short enough that the width between two is a straight run
    double walked = 0.0;
    steps.append({run.first(), s.lineWidthAt(0.0) / 2.0});
    for (int i = 1; i < run.size(); ++i) {
        const QPointF a = run.at(i - 1), b = run.at(i);
        const double len = QLineF(a, b).length();
        if (len <= 0.0)
            continue;
        const int parts = std::max(1, int(std::ceil(len / stride)));
        for (int k = 1; k <= parts; ++k) {
            const double t = double(k) / parts;
            steps.append({a + (b - a) * t, s.lineWidthAt((walked + len * t) / length) / 2.0});
        }
        walked += len;
    }

    constexpr double tiny = 0.02;
    const auto disc = [&](const Step &st) {
        if (st.radius < tiny)
            return;
        const int sides = std::clamp(int(std::ceil(st.radius * 2.0)) + 8, 8, 64);
        QPolygonF ring;
        for (int k = 0; k < sides; ++k) {
            const double a = 2.0 * kPi * k / sides;
            ring << st.pos + QPointF(std::cos(a), std::sin(a)) * st.radius;
        }
        addPiece(out, ring);
    };
    disc(steps.first());
    for (int i = 1; i < steps.size(); ++i) {
        const Step &a = steps.at(i - 1), &b = steps.at(i);
        const QPointF d = b.pos - a.pos;
        const double len = std::hypot(d.x(), d.y());
        if (len > 0.0 && (a.radius >= tiny || b.radius >= tiny)) {
            const QPointF side(-d.y() / len, d.x() / len);
            addPiece(out, QPolygonF({a.pos + side * a.radius, b.pos + side * b.radius, b.pos - side * b.radius,
                                     a.pos - side * a.radius}));
        }
        disc(b);
    }
    return out;
}

} // namespace

double ShapeSettings::lineWidthAt(double along) const
{
    const double width = double(std::clamp(line, 0, MaxLine));
    if (!isTapered())
        return width;
    double a = std::clamp(taperStart, 0, 100) / 100.0, b = std::clamp(taperEnd, 0, 100) / 100.0;
    if (a + b > 1.0) {
        // They'd overlap: they meet where their shares of the line put them.
        const double sum = a + b;
        a /= sum;
        b /= sum;
    }
    // Quick to fill out from the tip, then easing into the full width: the
    // mark a pen leaves as it lands and lifts.
    const auto ease = [](double u) { return std::sin(std::clamp(u, 0.0, 1.0) * kPi / 2.0); };
    const double t = std::clamp(along, 0.0, 1.0);
    double f = 1.0;
    if (a > 0.0 && t < a)
        f = std::min(f, ease(t / a));
    if (b > 0.0 && t > 1.0 - b)
        f = std::min(f, ease((1.0 - t) / b));
    return width * f;
}

void ShapeSettings::setNode(int i, const ShapeNode &node)
{
    if (i < 0 || i >= points.size())
        return;
    while (nodes.size() <= i)
        nodes.append(ShapeNode());
    nodes[i] = node;
}

void ShapeSettings::insertPoint(int i, const QPointF &p)
{
    i = std::clamp(i, 0, int(points.size()));
    if (i < nodes.size())
        nodes.insert(i, ShapeNode());
    points.insert(i, p);
}

void ShapeSettings::removePoint(int i)
{
    if (i < 0 || i >= points.size())
        return;
    points.removeAt(i);
    if (i < nodes.size())
        nodes.removeAt(i);
}

QPointF ShapeSettings::handleOut(int i) const
{
    const int n = int(points.size());
    if (n < 2 || i < 0 || i >= n)
        return {};
    const ShapeNode nd = node(i);
    if (nd.kind == ShapeNode::Corner)
        return {};
    if (nd.kind == ShapeNode::Handle)
        return nd.out;
    // A Catmull-Rom spline's: a third of the way along the line between the
    // points either side (halved, as that line spans two stretches).
    const bool loop = isLoop();
    const auto at = [&](int k) {
        if (loop)
            return points.at(((k % n) + n) % n);
        return points.at(std::clamp(k, 0, n - 1));
    };
    return (at(i + 1) - at(i - 1)) / 6.0;
}

QPainterPath ShapeSettings::path() const
{
    QPainterPath path;
    const int n = int(points.size());
    if (n < 2)
        return path;
    const bool loop = isLoop();
    // Two points make a straight line, unless one has been aimed by hand.
    const bool aimed = std::any_of(nodes.cbegin(), nodes.cbegin() + std::min(int(nodes.size()), n),
                                   [](const ShapeNode &nd) { return nd.kind == ShapeNode::Handle; });
    if (!curved || (n < 3 && !aimed)) {
        path.moveTo(points.first());
        for (int i = 1; i < n; ++i)
            path.lineTo(points.at(i));
        if (loop)
            path.closeSubpath();
        return path;
    }
    // One cubic for each stretch between two points. With every point Smooth
    // it's a Catmull-Rom spline: through every point, each stretch's handles
    // coming from the points either side.
    path.moveTo(points.first());
    const int stretches = loop ? n : n - 1;
    for (int i = 0; i < stretches; ++i) {
        const int j = (i + 1) % n;
        const QPointF p1 = points.at(i), p2 = points.at(j);
        path.cubicTo(p1 + handleOut(i), p2 - handleOut(j), p2);
    }
    if (loop)
        path.closeSubpath();
    return path;
}

void ShapeSettings::translate(const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
    gradientFrom += delta;
    gradientTo += delta;
}

Gradient ShapeSettings::gradient() const
{
    Gradient g;
    g.stops = normalizedStops(gradientStops);
    g.shape = gradientShape;
    if (gradientPlaced) {
        g.from = gradientFrom;
        g.to = gradientTo;
        return g;
    }
    const QRectF r = path().boundingRect();
    const QPointF c = r.center();
    switch (gradientShape) {
    case GradientShape::Linear:
        g.from = QPointF(c.x(), r.top());
        g.to = QPointF(c.x(), r.bottom());
        break;
    case GradientShape::Reflected:
        g.from = c;
        g.to = QPointF(c.x(), r.bottom());
        break;
    case GradientShape::Radial:
        g.from = c;
        g.to = r.bottomRight(); // the far corner: the last colour only at the very edge
        break;
    case GradientShape::Conical:
        g.from = c;
        g.to = QPointF(r.right(), c.y());
        break;
    }
    return g;
}

QJsonObject ShapeSettings::toJson() const
{
    QJsonArray list;
    for (const QPointF &p : points)
        list.append(QJsonArray{p.x(), p.y()});
    QJsonObject o{
        {QLatin1String("points"), list},
        {QLatin1String("closed"), closed},
        {QLatin1String("curved"), curved},
        {QLatin1String("line"), line},
        {QLatin1String("lineColor"), lineColor.name(QColor::HexArgb)},
        {QLatin1String("filled"), filled},
        {QLatin1String("fill"), fill.name(QColor::HexArgb)},
        {QLatin1String("smooth"), smooth},
    };
    if (taperStart > 0 || taperEnd > 0)
        o.insert(QLatin1String("taper"), QJsonArray{taperStart, taperEnd});
    // Written only when a point isn't Smooth: 0 for one that is, 1 for a
    // Corner, the handle's [x, y] for one aimed by hand.
    if (std::any_of(nodes.cbegin(), nodes.cend(), [](const ShapeNode &nd) { return nd.kind != ShapeNode::Smooth; })) {
        QJsonArray kinds;
        for (int i = 0; i < points.size(); ++i) {
            const ShapeNode nd = node(i);
            if (nd.kind == ShapeNode::Handle)
                kinds.append(QJsonArray{nd.out.x(), nd.out.y()});
            else
                kinds.append(nd.kind == ShapeNode::Corner ? 1 : 0);
        }
        o.insert(QLatin1String("nodes"), kinds);
    }
    if (hasGradient()) {
        QJsonObject g{
            {QLatin1String("stops"), stopsToString(gradientStops)},
            {QLatin1String("shape"), gradientShapeKey(gradientShape)},
        };
        if (gradientPlaced) {
            g.insert(QLatin1String("from"), QJsonArray{gradientFrom.x(), gradientFrom.y()});
            g.insert(QLatin1String("to"), QJsonArray{gradientTo.x(), gradientTo.y()});
        }
        o.insert(QLatin1String("gradient"), g);
    }
    return o;
}

ShapeSettings ShapeSettings::fromJson(const QJsonObject &o)
{
    ShapeSettings s;
    constexpr double far = 1.0e6;
    const QJsonArray list = o.value(QLatin1String("points")).toArray();
    const QJsonArray kinds = o.value(QLatin1String("nodes")).toArray();
    for (int i = 0; i < list.size(); ++i) {
        const QJsonArray p = list.at(i).toArray();
        if (p.size() != 2 || s.points.size() >= MaxPoints)
            continue;
        const double x = p.at(0).toDouble(), y = p.at(1).toDouble();
        if (!std::isfinite(x) || !std::isfinite(y))
            continue;
        s.points.append(QPointF(std::clamp(x, -far, far), std::clamp(y, -far, far)));
        // Its node, kept in step with the points that were taken.
        ShapeNode nd;
        const QJsonValue k = i < kinds.size() ? kinds.at(i) : QJsonValue();
        if (const QJsonArray h = k.toArray(); h.size() == 2) {
            const double hx = h.at(0).toDouble(), hy = h.at(1).toDouble();
            if (std::isfinite(hx) && std::isfinite(hy)) {
                nd.kind = ShapeNode::Handle;
                nd.out = QPointF(std::clamp(hx, -far, far), std::clamp(hy, -far, far));
            }
        } else if (k.toInt() == 1) {
            nd.kind = ShapeNode::Corner;
        }
        if (nd.kind != ShapeNode::Smooth)
            s.setNode(int(s.points.size()) - 1, nd);
    }
    if (const QJsonArray taper = o.value(QLatin1String("taper")).toArray(); taper.size() == 2) {
        s.taperStart = std::clamp(taper.at(0).toInt(), 0, 100);
        s.taperEnd = std::clamp(taper.at(1).toInt(), 0, 100);
    }
    s.closed = o.value(QLatin1String("closed")).toBool(true);
    s.curved = o.value(QLatin1String("curved")).toBool(false);
    s.line = std::clamp(o.value(QLatin1String("line")).toInt(s.line), 0, MaxLine);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("lineColor")).toString()); c.isValid())
        s.lineColor = c;
    s.filled = o.value(QLatin1String("filled")).toBool(false);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("fill")).toString()); c.isValid())
        s.fill = c;
    s.smooth = o.value(QLatin1String("smooth")).toBool(true);
    if (const QJsonValue gradient = o.value(QLatin1String("gradient")); gradient.isObject()) {
        const QJsonObject g = gradient.toObject();
        s.gradientStops = stopsFromString(g.value(QLatin1String("stops")).toString());
        s.gradientShape = gradientShapeFromKey(g.value(QLatin1String("shape")).toString());
        s.gradientFill = s.gradientStops.size() >= 2;
        const QJsonArray from = g.value(QLatin1String("from")).toArray();
        const QJsonArray to = g.value(QLatin1String("to")).toArray();
        const auto ok = [&](const QJsonArray &a) {
            return a.size() == 2 && std::isfinite(a.at(0).toDouble()) && std::isfinite(a.at(1).toDouble())
                   && std::abs(a.at(0).toDouble()) <= far && std::abs(a.at(1).toDouble()) <= far;
        };
        if (s.gradientFill && ok(from) && ok(to)) {
            s.gradientPlaced = true;
            s.gradientFrom = QPointF(from.at(0).toDouble(), from.at(1).toDouble());
            s.gradientTo = QPointF(to.at(0).toDouble(), to.at(1).toDouble());
        }
    }
    return s;
}

ShapeLayout layoutShape(const ShapeSettings &s)
{
    ShapeLayout out;
    if (!s.isDrawable())
        return out;
    const QPainterPath path = s.path();
    const bool loop = s.isLoop();
    const bool fill = s.filled && loop;
    // With neither an outline nor a fill there'd be nothing to see: a hairline then.
    const double line = double(std::clamp(s.line, 0, ShapeSettings::MaxLine));
    const double width = line > 0.0 ? line : fill ? 0.0 : 1.0;
    // A sharp corner's line runs on past the corner, up to the miter limit.
    const double reach = width * (s.curved ? 0.5 : 2.0) + 2.0;
    const QRect bounds = path.boundingRect().adjusted(-reach, -reach, reach, reach).toAlignedRect();
    if (bounds.width() <= 0 || bounds.height() <= 0 || bounds.width() > kMaxSide || bounds.height() > kMaxSide)
        return out;

    const auto harden = [&](QImage &img) {
        if (s.smooth)
            return;
        // No in-between pixels at all: a pixel is there or it isn't.
        for (int y = 0; y < img.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x)
                row[x] = qAlpha(row[x]) < 128 ? 0u : (qUnpremultiply(row[x]) | 0xff000000u);
        }
    };
    const bool gradient = fill && s.hasGradient();
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        // The outline, with a flat fill under it. A gradient fill is worked
        // out below, in the tiles' own precision, and goes under this.
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, s.smooth);
        p.translate(-bounds.topLeft());
        if (fill && !gradient)
            p.fillPath(path, QColor(s.fill.red(), s.fill.green(), s.fill.blue()));
        if (width > 0.0 && s.isTapered())
            p.fillPath(taperedLine(s, path), QColor(s.lineColor.red(), s.lineColor.green(), s.lineColor.blue()));
        else if (width > 0.0)
            p.strokePath(path, penFor(s, width));
    }
    harden(image);
    out.image = fromClipboardImage(image);
    out.origin = bounds.topLeft();
    if (!gradient)
        return out;

    // How much of each pixel the shape covers, then the gradient's colour
    // there by that much, with the outline over it.
    QImage cover(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    cover.fill(Qt::transparent);
    {
        QPainter p(&cover);
        p.setRenderHint(QPainter::Antialiasing, s.smooth);
        p.translate(-bounds.topLeft());
        p.fillPath(path, Qt::white);
    }
    harden(cover);
    const Gradient g = s.gradient();
    for (int y = 0; y < cover.height(); ++y) {
        const auto *in = reinterpret_cast<const QRgb *>(cover.constScanLine(y));
        auto *px = reinterpret_cast<Pixel *>(out.image.scanLine(y));
        for (int x = 0; x < cover.width(); ++x) {
            const float c = float(qAlpha(in[x])) / 255.0f;
            if (c <= 0.0f)
                continue;
            const Pixel under = gradientPixel(g.stops, gradientPosition(g, x + bounds.left(), y + bounds.top()));
            const float keep = 1.0f - float(px[x].a);
            px[x] = makePixel(float(px[x].r) + float(under.r) * c * keep, float(px[x].g) + float(under.g) * c * keep,
                              float(px[x].b) + float(under.b) * c * keep, float(px[x].a) + float(under.a) * c * keep);
        }
    }
    return out;
}

bool shapeHit(const ShapeSettings &s, const QPointF &pos, double tolerance)
{
    if (!s.isDrawable())
        return false;
    const QPainterPath path = s.path();
    if (s.isLoop() && path.contains(pos))
        return true;
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(double(s.line), 1.0) + 2.0 * tolerance);
    return stroker.createStroke(path).contains(pos);
}

int shapePointAt(const ShapeSettings &s, const QPointF &pos, double tolerance)
{
    int best = -1;
    double bestDistance = tolerance;
    for (int i = 0; i < s.points.size(); ++i) {
        const double d = QLineF(pos, s.points.at(i)).length();
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

int shapeSideAt(const ShapeSettings &s, const QPointF &pos, double tolerance)
{
    const int n = int(s.points.size());
    if (n < 2)
        return -1;
    const int sides = s.isLoop() ? n : n - 1;
    int best = -1;
    double bestDistance = tolerance;
    for (int i = 0; i < sides; ++i) {
        const double d = distanceToSegment(pos, s.points.at(i), s.points.at((i + 1) % n));
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

} // namespace easeletch
