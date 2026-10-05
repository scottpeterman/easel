#include "shapes.h"

#include "regionops.h"

#include <QJsonArray>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>

namespace easeletch {

namespace {

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

} // namespace

QPainterPath ShapeSettings::path() const
{
    QPainterPath path;
    const int n = int(points.size());
    if (n < 2)
        return path;
    const bool loop = closed && n >= 3;
    if (!curved || n < 3) {
        path.moveTo(points.first());
        for (int i = 1; i < n; ++i)
            path.lineTo(points.at(i));
        if (loop)
            path.closeSubpath();
        return path;
    }
    // A Catmull-Rom spline: it passes through every point, and each stretch
    // is one cubic whose handles come from the points either side.
    const auto at = [&](int i) {
        if (loop)
            return points.at(((i % n) + n) % n);
        return points.at(std::clamp(i, 0, n - 1));
    };
    path.moveTo(points.first());
    const int stretches = loop ? n : n - 1;
    for (int i = 0; i < stretches; ++i) {
        const QPointF p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    if (loop)
        path.closeSubpath();
    return path;
}

void ShapeSettings::translate(const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
}

QJsonObject ShapeSettings::toJson() const
{
    QJsonArray list;
    for (const QPointF &p : points)
        list.append(QJsonArray{p.x(), p.y()});
    return {
        {QLatin1String("points"), list},
        {QLatin1String("closed"), closed},
        {QLatin1String("curved"), curved},
        {QLatin1String("line"), line},
        {QLatin1String("lineColor"), lineColor.name(QColor::HexArgb)},
        {QLatin1String("filled"), filled},
        {QLatin1String("fill"), fill.name(QColor::HexArgb)},
        {QLatin1String("smooth"), smooth},
    };
}

ShapeSettings ShapeSettings::fromJson(const QJsonObject &o)
{
    ShapeSettings s;
    constexpr double far = 1.0e6;
    const QJsonArray list = o.value(QLatin1String("points")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonArray p = v.toArray();
        if (p.size() != 2 || s.points.size() >= MaxPoints)
            continue;
        const double x = p.at(0).toDouble(), y = p.at(1).toDouble();
        if (std::isfinite(x) && std::isfinite(y))
            s.points.append(QPointF(std::clamp(x, -far, far), std::clamp(y, -far, far)));
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
    return s;
}

ShapeLayout layoutShape(const ShapeSettings &s)
{
    ShapeLayout out;
    if (!s.isDrawable())
        return out;
    const QPainterPath path = s.path();
    const bool loop = s.closed && s.points.size() >= 3;
    const bool fill = s.filled && loop;
    // With neither an outline nor a fill there'd be nothing to see: a hairline then.
    const double line = double(std::clamp(s.line, 0, ShapeSettings::MaxLine));
    const double width = line > 0.0 ? line : fill ? 0.0 : 1.0;
    // A sharp corner's line runs on past the corner, up to the miter limit.
    const double reach = width * (s.curved ? 0.5 : 2.0) + 2.0;
    const QRect bounds = path.boundingRect().adjusted(-reach, -reach, reach, reach).toAlignedRect();
    if (bounds.width() <= 0 || bounds.height() <= 0 || bounds.width() > kMaxSide || bounds.height() > kMaxSide)
        return out;

    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, s.smooth);
    p.translate(-bounds.topLeft());
    if (fill)
        p.fillPath(path, QColor(s.fill.red(), s.fill.green(), s.fill.blue()));
    if (width > 0.0)
        p.strokePath(path, penFor(s, width));
    p.end();
    if (!s.smooth) {
        // No in-between pixels at all: a pixel is there or it isn't.
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                row[x] = qAlpha(row[x]) < 128 ? 0u : (qUnpremultiply(row[x]) | 0xff000000u);
        }
    }
    out.image = fromClipboardImage(image);
    out.origin = bounds.topLeft();
    return out;
}

bool shapeHit(const ShapeSettings &s, const QPointF &pos, double tolerance)
{
    if (!s.isDrawable())
        return false;
    const QPainterPath path = s.path();
    if (s.closed && s.points.size() >= 3 && path.contains(pos))
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
    const int sides = s.closed && n >= 3 ? n : n - 1;
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
