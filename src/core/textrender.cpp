#include "textrender.h"

#include "regionops.h"
#include "tilestore.h"

#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace easeletch {

namespace {

// Larger than this and the image is no use on any canvas.
constexpr int kMaxSide = 16384;

QFont fontFor(const TextSettings &s)
{
    QFont f;
    if (!s.family.isEmpty())
        f.setFamily(s.family);
    f.setPixelSize(std::clamp(s.pixelSize, TextSettings::MinSize, TextSettings::MaxSize));
    f.setBold(s.bold);
    f.setItalic(s.italic);
    // Letter shapes shouldn't depend on the screen's hinting settings.
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(s.smooth ? QFont::PreferAntialias : QFont::NoAntialias);
    return f;
}

// One line of the text as the lines it takes inside a width: broken between
// words; a word wider than the box gets a line to itself and sticks out.
QStringList wrapLine(const QString &line, const QFontMetricsF &metrics, double width)
{
    QStringList out;
    QString current;
    const QStringList words = line.split(QLatin1Char(' '));
    for (const QString &word : words) {
        const QString trial = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
        if (!current.isEmpty() && metrics.horizontalAdvance(trial) > width) {
            out.append(current);
            current = word;
        } else {
            current = trial;
        }
    }
    out.append(current);
    return out;
}

constexpr const char *kFrameKeys[FrameStyleCount] = {"",        "single",  "double", "rounded", "corners", "notched",
                                                     "looped",  "speech",  "whisper", "thought", "shout"};

// How a frame is laid out round the block of text it holds.
struct FrameGeometry {
    double line = 0.0;   // the main line's width
    double fine = 0.0;   // Double: the inner line's width
    double gap = 0.0;    // Double: how far the heavy line sits outside the fine one
    double radius = 0.0; // Rounded, Notched, Looped: the corner's radius
    double arm = 0.0;    // Corners: the length of each mark's arms
    double reach = 0.0;  // how far the decoration goes beyond the padded block
};

FrameGeometry frameGeometry(const TextFrame &frame, const QSizeF &padded)
{
    FrameGeometry g;
    if (!frame.isActive())
        return g;
    g.line = double(std::clamp(frame.line, TextFrame::MinLine, TextFrame::MaxLine));
    const double shortSide = std::min(padded.width(), padded.height());
    g.reach = g.line / 2.0 + 1.0;
    switch (frame.style) {
    case FrameStyle::Double:
        g.fine = std::max(1.0, g.line / 3.0);
        g.gap = g.line + g.fine + 1.0;
        g.reach = g.gap + g.line / 2.0 + 1.0;
        break;
    case FrameStyle::Rounded:
        g.radius = std::clamp(shortSide * 0.25, 4.0, 60.0);
        break;
    case FrameStyle::Corners:
        g.arm = std::clamp(shortSide * 0.3, 3.0 * g.line, 80.0);
        break;
    case FrameStyle::Notched:
        g.radius = std::clamp(shortSide * 0.2, 2.0 * g.line, 48.0);
        break;
    case FrameStyle::Looped:
        g.radius = std::clamp(shortSide * 0.14, 2.0 * g.line, 36.0);
        g.reach = g.radius + g.line / 2.0 + 1.0;
        break;
    default:
        break;
    }
    return g;
}

// A rectangle whose corners are each replaced by a quarter circle cut into it
// (sweep 90) or a loop round the outside of it (sweep -270).
QPainterPath cornerPath(const QRectF &r, double radius, double sweep)
{
    const double d = 2.0 * radius;
    const auto circle = [&](const QPointF &c) { return QRectF(c.x() - radius, c.y() - radius, d, d); };
    QPainterPath path;
    path.moveTo(r.left() + radius, r.top());
    path.lineTo(r.right() - radius, r.top());
    path.arcTo(circle(r.topRight()), 180.0, sweep);
    path.lineTo(r.right(), r.bottom() - radius);
    path.arcTo(circle(r.bottomRight()), 90.0, sweep);
    path.lineTo(r.left() + radius, r.bottom());
    path.arcTo(circle(r.bottomLeft()), 0.0, sweep);
    path.lineTo(r.left(), r.top() + radius);
    path.arcTo(circle(r.topLeft()), 270.0, sweep);
    path.closeSubpath();
    return path;
}

// Draws one of the stencil frames round r, the block of text with its padding.
void drawFrame(QPainter &p, const TextFrame &frame, const FrameGeometry &g, const QRectF &r)
{
    const QColor lineColor(frame.lineColor.red(), frame.lineColor.green(), frame.lineColor.blue());
    const QColor fill(frame.fill.red(), frame.fill.green(), frame.fill.blue());
    const QPen pen(lineColor, g.line, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.save();
    p.setBrush(Qt::NoBrush);
    QPainterPath shape; // what the fill covers and the line follows
    switch (frame.style) {
    case FrameStyle::Notched:
        shape = cornerPath(r, g.radius, 90.0);
        break;
    case FrameStyle::Double:
        shape.addRect(r.adjusted(-g.gap, -g.gap, g.gap, g.gap));
        break;
    default:
        shape.addRect(r);
        break;
    }
    if (frame.filled) {
        // Inside the loops stays clear: the fill stops at the corners they go round.
        p.fillPath(frame.style == FrameStyle::Looped ? cornerPath(r, g.radius, 90.0) : shape, fill);
    }
    p.setPen(pen);
    switch (frame.style) {
    case FrameStyle::Corners: {
        const double a = g.arm;
        const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        const QPointF along[4] = {{a, 0}, {0, a}, {-a, 0}, {0, -a}};
        for (int i = 0; i < 4; ++i) {
            // Each mark: an arm back along the side arriving, one on along the side leaving.
            QPainterPath mark;
            mark.moveTo(corners[i] - along[(i + 3) % 4]);
            mark.lineTo(corners[i]);
            mark.lineTo(corners[i] + along[i]);
            p.drawPath(mark);
        }
        break;
    }
    case FrameStyle::Looped:
        p.setPen(QPen(lineColor, g.line, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(cornerPath(r, g.radius, -270.0));
        break;
    case FrameStyle::Double:
        p.drawPath(shape);
        p.setPen(QPen(lineColor, g.fine, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawRect(r);
        break;
    default:
        p.drawPath(shape);
        break;
    }
    p.restore();
}

// --- Balloons -----------------------------------------------------------------

// count points round an ellipse, the same distance apart along its edge
// (equal angles would crowd them at the ends of a wide one). Clockwise on
// screen, from the rightmost point.
QList<QPointF> aroundEllipse(const QPointF &c, double a, double b, int count)
{
    constexpr int kSamples = 720;
    std::vector<double> length(kSamples + 1, 0.0);
    QPointF last(c.x() + a, c.y());
    for (int i = 1; i <= kSamples; ++i) {
        const double t = 2.0 * std::numbers::pi * i / kSamples;
        const QPointF pt(c.x() + a * std::cos(t), c.y() + b * std::sin(t));
        length[size_t(i)] = length[size_t(i - 1)] + std::hypot(pt.x() - last.x(), pt.y() - last.y());
        last = pt;
    }
    QList<QPointF> out;
    int at = 0;
    for (int i = 0; i < count; ++i) {
        const double want = length[kSamples] * i / count;
        while (at < kSamples && length[size_t(at + 1)] < want)
            ++at;
        const double span = length[size_t(at + 1)] - length[size_t(at)];
        const double t = 2.0 * std::numbers::pi * (at + (span > 0.0 ? (want - length[size_t(at)]) / span : 0.0)) / kSamples;
        out.append(QPointF(c.x() + a * std::cos(t), c.y() + b * std::sin(t)));
    }
    return out;
}

double ellipsePerimeter(double a, double b)
{
    // Ramanujan's approximation: plenty for counting bumps.
    return std::numbers::pi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
}

// A balloon (or a rounded box) as it's drawn: the shape with its tail, the
// separate bubbles of a thought's tail, and how much room it all takes.
struct Balloon {
    QPainterPath shape;          // the body, with a tail joined on
    QList<QPainterPath> bubbles; // Thought: the trail to the tip
    QPen pen;
    QRectF body;                 // what the body alone covers, line included
    QRectF all;                  // ... and with the tail
    bool hasTail = false;
    QPointF tip;
};

// block: the block of words; everything is in its coordinates.
Balloon buildBalloon(const TextFrame &frame, const QRectF &block)
{
    Balloon out;
    const double line = double(std::clamp(frame.line, TextFrame::MinLine, TextFrame::MaxLine));
    const double padding = double(std::clamp(frame.padding, 0, TextFrame::MaxPadding));
    const QColor lineColor(frame.lineColor.red(), frame.lineColor.green(), frame.lineColor.blue());
    const QPointF c = block.center();
    // An oval through the corners of the words, then the padding beyond that.
    const double a = block.width() / 2.0 * std::numbers::sqrt2 + padding;
    const double b = block.height() / 2.0 * std::numbers::sqrt2 + padding;
    out.pen = QPen(lineColor, line, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    double bump = 0.0; // Thought: the size of the cloud's bumps
    double beyond = line / 2.0 + 1.0; // how far the line reaches past the path

    QPainterPath body;
    switch (frame.style) {
    case FrameStyle::Rounded: {
        const QRectF r = block.adjusted(-padding, -padding, padding, padding);
        const double radius = std::clamp(std::min(r.width(), r.height()) * 0.25, 4.0, 60.0);
        body.addRoundedRect(r, radius, radius);
        out.pen = QPen(lineColor, line, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        break;
    }
    case FrameStyle::Thought: {
        bump = std::clamp(std::min(a, b) * 0.42, 8.0, 70.0);
        const int count = std::max(8, int(std::lround(ellipsePerimeter(a, b) / (bump * 1.45))));
        body.addEllipse(c, a, b);
        const QList<QPointF> centres = aroundEllipse(c, std::max(a - bump * 0.45, 1.0), std::max(b - bump * 0.45, 1.0),
                                                     count);
        for (const QPointF &pt : centres) {
            QPainterPath one;
            one.addEllipse(pt, bump, bump);
            body = body.united(one);
        }
        break;
    }
    case FrameStyle::Shout: {
        const int spikes = std::clamp(int(std::lround(ellipsePerimeter(a, b) / std::max(30.0, std::min(a, b) * 0.55))),
                                      10, 32);
        const QList<QPointF> ring = aroundEllipse(c, a, b, spikes * 2);
        QPolygonF burst;
        for (int i = 0; i < ring.size(); ++i) {
            // Every other point is a spike's tip, no two neighbours the same
            // length, so it looks drawn and not stamped.
            const double stretch = i % 2 == 0 ? 1.30 + 0.14 * double((i / 2 * 7) % 5) / 4.0 : 1.0;
            burst << c + (ring.at(i) - c) * stretch;
        }
        body.addPolygon(burst);
        body.closeSubpath();
        out.pen = QPen(lineColor, line, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        out.pen.setMiterLimit(4.0);
        beyond = line * 2.0 + 1.0;
        break;
    }
    default: // Speech, Whisper
        body.addEllipse(c, a, b);
        break;
    }
    if (frame.style == FrameStyle::Whisper) {
        out.pen.setCapStyle(Qt::FlatCap);
        out.pen.setDashPattern({3.0, 2.2});
    }
    out.body = body.boundingRect().adjusted(-beyond, -beyond, beyond, beyond);
    out.shape = body;
    out.all = out.body;

    if (!frame.hasTail())
        return out;
    const QRectF bounds = body.boundingRect();
    QPointF tip = c + QPointF(frame.tailOffset);
    if (frame.tailOffset.isNull())
        tip = QPointF(c.x() - bounds.width() * 0.18,
                      bounds.bottom() + std::clamp(bounds.height() * 0.45, 30.0, 140.0));
    out.hasTail = true;
    out.tip = tip;
    if (body.contains(tip))
        return out; // pointing at itself: nothing to draw, but the handle is still there to drag out
    // Where the way from the middle to the tip leaves the body.
    double inside = 0.0, outside = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = (inside + outside) / 2.0;
        (body.contains(c + (tip - c) * mid) ? inside : outside) = mid;
    }
    const QPointF edge = c + (tip - c) * inside;
    const double length = std::hypot(tip.x() - edge.x(), tip.y() - edge.y());
    if (length < 1.0)
        return out;
    const QPointF dir = (tip - edge) / length;
    const QPointF across(-dir.y(), dir.x());

    if (frame.style == FrameStyle::Thought) {
        // Bubbles, smaller as they go, the last one at the tip.
        const double radii[3] = {bump * 0.5, bump * 0.34, bump * 0.22};
        const double first = radii[0] * 1.5;
        const double end = length - radii[2];
        const int count = end < first ? 1 : end < first + radii[1] * 3.0 ? 2 : 3;
        for (int i = 0; i < count; ++i) {
            const double at = count == 1 ? std::max(end, radii[2])
                                         : first + (end - first) * double(i) / double(count - 1);
            QPainterPath bubble;
            const double r = radii[count == 1 ? 2 : i == count - 1 ? 2 : i];
            bubble.addEllipse(edge + dir * at, r, r);
            out.bubbles.append(bubble);
            out.all |= bubble.boundingRect().adjusted(-beyond, -beyond, beyond, beyond);
        }
        return out;
    }
    // A wedge from inside the body to the tip, joined on so the line runs
    // round both; a speech tail leans a little, a shout's is straight.
    const double half = std::clamp(std::min(bounds.width(), bounds.height()) * 0.13, line * 1.5 + 3.0, 34.0);
    const QPointF root = edge - dir * std::min(half * 1.6, inside * std::hypot(tip.x() - c.x(), tip.y() - c.y()));
    const QPointF one = root + across * half, two = root - across * half;
    const double bend = frame.style == FrameStyle::Shout || frame.style == FrameStyle::Rounded ? 0.0 : length * 0.14;
    const QPointF lean = across * (dir.x() >= 0.0 ? bend : -bend);
    QPainterPath wedge;
    wedge.moveTo(one);
    wedge.quadTo((one + tip) / 2.0 + lean, tip);
    wedge.quadTo((two + tip) / 2.0 + lean, two);
    wedge.closeSubpath();
    out.shape = body.united(wedge);
    const double tipReach = std::max(beyond, line * 2.0 + 1.0); // a sharp point's line runs on past it
    out.all = out.body | wedge.boundingRect().adjusted(-beyond, -beyond, beyond, beyond)
              | QRectF(tip, QSizeF(0.0, 0.0)).adjusted(-tipReach, -tipReach, tipReach, tipReach);
    return out;
}

void drawBalloon(QPainter &p, const TextFrame &frame, const Balloon &balloon)
{
    const QColor fill(frame.fill.red(), frame.fill.green(), frame.fill.blue());
    p.save();
    p.setBrush(frame.filled ? QBrush(fill) : QBrush(Qt::NoBrush));
    p.setPen(balloon.pen);
    p.drawPath(balloon.shape);
    for (const QPainterPath &bubble : balloon.bubbles)
        p.drawPath(bubble);
    p.restore();
}

bool drawnAsBalloon(FrameStyle style)
{
    return style == FrameStyle::Rounded || TextFrame::isBalloon(style);
}

QString alignKey(Qt::Alignment align)
{
    return QLatin1String(align & Qt::AlignHCenter ? "centre" : align & Qt::AlignRight ? "right" : "left");
}

} // namespace

QString frameStyleKey(FrameStyle style)
{
    const int i = int(style);
    return QLatin1String(i >= 0 && i < FrameStyleCount ? kFrameKeys[i] : "");
}

FrameStyle frameStyleFromKey(const QString &key)
{
    for (int i = 1; i < FrameStyleCount; ++i)
        if (key == QLatin1String(kFrameKeys[i]))
            return FrameStyle(i);
    return FrameStyle::None;
}

QJsonObject TextFrame::toJson() const
{
    QJsonObject o{
        {QLatin1String("style"), frameStyleKey(style)},
        {QLatin1String("line"), line},
        {QLatin1String("lineColor"), lineColor.name(QColor::HexArgb)},
        {QLatin1String("filled"), filled},
        {QLatin1String("fill"), fill.name(QColor::HexArgb)},
        {QLatin1String("padding"), padding},
    };
    if (hasTail())
        o.insert(QLatin1String("tail"), QJsonArray{tailOffset.x(), tailOffset.y()});
    return o;
}

bool TextFrame::isBalloon(FrameStyle style)
{
    return style == FrameStyle::Speech || style == FrameStyle::Whisper || style == FrameStyle::Thought
           || style == FrameStyle::Shout;
}

bool TextFrame::canHaveTail(FrameStyle style)
{
    return style == FrameStyle::Rounded || isBalloon(style);
}

TextFrame TextFrame::fromJson(const QJsonObject &o)
{
    TextFrame f;
    f.style = frameStyleFromKey(o.value(QLatin1String("style")).toString());
    f.line = std::clamp(o.value(QLatin1String("line")).toInt(f.line), MinLine, MaxLine);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("lineColor")).toString()); c.isValid())
        f.lineColor = c;
    f.filled = o.value(QLatin1String("filled")).toBool(false);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("fill")).toString()); c.isValid())
        f.fill = c;
    f.padding = std::clamp(o.value(QLatin1String("padding")).toInt(f.padding), 0, MaxPadding);
    if (const QJsonArray tail = o.value(QLatin1String("tail")).toArray(); tail.size() == 2) {
        constexpr int far = 100000;
        f.tail = true;
        f.tailOffset = QPoint(std::clamp(tail.at(0).toInt(), -far, far), std::clamp(tail.at(1).toInt(), -far, far));
    }
    return f;
}

QJsonObject TextSettings::toJson() const
{
    QJsonObject o{
        {QLatin1String("text"), text},
        {QLatin1String("family"), family},
        {QLatin1String("size"), pixelSize},
        {QLatin1String("bold"), bold},
        {QLatin1String("italic"), italic},
        {QLatin1String("align"), alignKey(align)},
        {QLatin1String("smooth"), smooth},
        {QLatin1String("color"), color.name(QColor::HexArgb)},
        {QLatin1String("outline"), outline},
        {QLatin1String("outlineColor"), outlineColor.name(QColor::HexArgb)},
        {QLatin1String("boxWidth"), boxWidth},
    };
    if (frame.isActive())
        o.insert(QLatin1String("frame"), frame.toJson());
    return o;
}

TextSettings TextSettings::fromJson(const QJsonObject &o)
{
    TextSettings s;
    s.text = o.value(QLatin1String("text")).toString();
    s.family = o.value(QLatin1String("family")).toString();
    s.pixelSize = std::clamp(o.value(QLatin1String("size")).toInt(s.pixelSize), MinSize, MaxSize);
    s.bold = o.value(QLatin1String("bold")).toBool(false);
    s.italic = o.value(QLatin1String("italic")).toBool(false);
    const QString align = o.value(QLatin1String("align")).toString();
    s.align = align == QLatin1String("centre") ? Qt::AlignHCenter
              : align == QLatin1String("right") ? Qt::AlignRight
                                                 : Qt::AlignLeft;
    s.smooth = o.value(QLatin1String("smooth")).toBool(true);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("color")).toString()); c.isValid())
        s.color = c;
    s.outline = std::clamp(o.value(QLatin1String("outline")).toInt(0), 0, MaxOutline);
    if (const QColor c = QColor::fromString(o.value(QLatin1String("outlineColor")).toString()); c.isValid())
        s.outlineColor = c;
    s.boxWidth = std::clamp(o.value(QLatin1String("boxWidth")).toInt(0), 0, MaxBoxWidth);
    if (const QJsonValue frame = o.value(QLatin1String("frame")); frame.isObject())
        s.frame = TextFrame::fromJson(frame.toObject());
    return s;
}

QImage renderText(const TextSettings &settings, QPoint *inset, int *width, QRect *frame)
{
    TextMetrics metrics;
    const QImage image = renderText(settings, &metrics);
    if (image.isNull())
        return image;
    if (inset)
        *inset = metrics.inset;
    if (width)
        *width = metrics.width;
    if (frame)
        *frame = metrics.frame;
    return image;
}

QImage renderText(const TextSettings &settings, TextMetrics *metrics)
{
    QStringList lines = settings.text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return {};
    const bool anything = std::any_of(lines.cbegin(), lines.cend(),
                                      [](const QString &l) { return !l.trimmed().isEmpty(); });
    if (!anything)
        return {};

    const QFont font = fontFor(settings);
    const QFontMetricsF fm(font);
    const int box = std::clamp(settings.boxWidth, 0, TextSettings::MaxBoxWidth);
    if (box > 0) {
        QStringList wrapped;
        for (const QString &line : std::as_const(lines))
            wrapped += wrapLine(line, fm, double(box));
        lines = wrapped;
    }
    double widest = double(box);
    for (const QString &line : std::as_const(lines))
        widest = std::max(widest, fm.horizontalAdvance(line));
    const int outline = std::clamp(settings.outline, 0, TextSettings::MaxOutline);
    // Italics and swashes reach past their advance width; an outline reaches
    // further by its own width, on every side.
    const int room = int(std::ceil(fm.height() * 0.3)) + 1;
    const double lineHeight = fm.lineSpacing();
    // A frame goes round the block of lines, its padding away, and needs
    // room of its own beyond that for its line, whatever its corners do, and
    // its tail. Worked out with the block's top-left at (0, 0).
    const bool framed = settings.frame.isActive();
    const bool balloon = framed && drawnAsBalloon(settings.frame.style);
    const double blockHeight = lineHeight * double(lines.size() - 1) + fm.ascent() + fm.descent();
    const QRectF block(0.0, 0.0, widest, blockHeight);
    const double padding = framed ? double(std::clamp(settings.frame.padding, 0, TextFrame::MaxPadding)) : 0.0;
    const QRectF padded = block.adjusted(-padding, -padding, padding, padding);
    const FrameGeometry geometry = frameGeometry(settings.frame, padded.size());
    Balloon shape;
    QRectF body, all; // what the frame covers, without and with its tail
    if (balloon) {
        shape = buildBalloon(settings.frame, block);
        body = shape.body;
        all = shape.all;
    } else if (framed) {
        body = all = padded.adjusted(-geometry.reach, -geometry.reach, geometry.reach, geometry.reach);
    }
    const int left = std::max(room + outline, framed ? int(std::ceil(-all.left())) : 0);
    const int right = std::max(room + outline, framed ? int(std::ceil(all.right() - widest)) : 0);
    const int top = std::max(room / 2 + outline, framed ? int(std::ceil(-all.top())) : 0);
    const int bottom = std::max(room - room / 2 + outline, framed ? int(std::ceil(all.bottom() - blockHeight)) : 0);
    const int w = int(std::ceil(widest)) + left + right;
    const int h = int(std::ceil(lineHeight * double(lines.size()) + fm.descent())) + top + bottom;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return {};
    if (metrics) {
        metrics->inset = QPoint(left, top);
        metrics->width = int(std::ceil(widest));
        metrics->centre = QPoint(left + int(std::lround(widest / 2.0)), top + int(std::lround(blockHeight / 2.0)));
        metrics->frame = framed ? body.translated(left, top).toAlignedRect().intersected(QRect(0, 0, w, h)) : QRect();
        metrics->hasTail = balloon && shape.hasTail;
        metrics->tailTip = metrics->hasTail ? (shape.tip + QPointF(left, top)).toPoint() : QPoint();
    }

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, settings.smooth);
    p.setRenderHint(QPainter::TextAntialiasing, settings.smooth);
    p.setFont(font);
    if (framed) {
        p.save();
        p.translate(left, top);
        if (balloon)
            drawBalloon(p, settings.frame, shape);
        else
            drawFrame(p, settings.frame, geometry, padded);
        p.restore();
    }
    const QColor ink(settings.color.red(), settings.color.green(), settings.color.blue());
    p.setPen(ink);
    QPainterPath letters;
    for (int i = 0; i < lines.size(); ++i) {
        const double advance = fm.horizontalAdvance(lines.at(i));
        double x = left;
        if (settings.align & Qt::AlignHCenter)
            x = left + (widest - advance) / 2.0;
        else if (settings.align & Qt::AlignRight)
            x = left + (widest - advance);
        const QPointF baseline(x, double(top) + fm.ascent() + lineHeight * i);
        if (outline > 0)
            letters.addText(baseline, font, lines.at(i));
        else
            p.drawText(baseline, lines.at(i));
    }
    if (outline > 0) {
        // The line goes down first, twice as wide and centred on the letters'
        // edge; the letters cover its inner half.
        const QColor line(settings.outlineColor.red(), settings.outlineColor.green(), settings.outlineColor.blue());
        p.strokePath(letters, QPen(line, 2.0 * outline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(letters, ink);
    }
    p.end();
    if (!settings.smooth) {
        // No in-between pixels at all: a pixel is there or it isn't.
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(row[x]) < 128)
                    row[x] = 0u;
                else if (outline > 0 || framed)
                    row[x] = qUnpremultiply(row[x]) | 0xff000000u;
                else
                    row[x] = settings.color.rgb() | 0xff000000u;
            }
        }
    }
    // Coverage becomes alpha as it is; the colour goes to linear light.
    return fromClipboardImage(image);
}

TextLayout layoutText(const TextSettings &settings, const QPoint &anchor)
{
    TextLayout out;
    TextMetrics m;
    out.image = renderText(settings, &m);
    if (out.image.isNull())
        return out;
    QPoint corner = anchor;
    if (settings.align & Qt::AlignHCenter)
        corner.rx() -= m.width / 2;
    else if (settings.align & Qt::AlignRight)
        corner.rx() -= m.width;
    out.origin = corner - m.inset;
    out.box = m.frame.isEmpty() ? QRect(corner.x(), out.origin.y(), std::max(m.width, 1), out.image.height())
                                : m.frame.translated(out.origin);
    out.centre = m.centre + out.origin;
    out.hasTail = m.hasTail;
    out.tailTip = m.tailTip + out.origin;
    return out;
}

QString resolvedFontFamily(const QString &family)
{
    QFont f;
    if (!family.isEmpty())
        f.setFamily(family);
    return QFontInfo(f).family();
}

} // namespace easeletch
