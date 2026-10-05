#include "textrender.h"

#include "regionops.h"
#include "tilestore.h"

#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>

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

constexpr const char *kFrameKeys[FrameStyleCount] = {"", "single", "double", "rounded", "corners", "notched", "looped"};

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

// Draws the frame round r, the block of text with its padding.
void drawFrame(QPainter &p, const TextFrame &frame, const FrameGeometry &g, const QRectF &r)
{
    const QColor lineColor(frame.lineColor.red(), frame.lineColor.green(), frame.lineColor.blue());
    const QColor fill(frame.fill.red(), frame.fill.green(), frame.fill.blue());
    const QPen pen(lineColor, g.line, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.save();
    p.setBrush(Qt::NoBrush);
    QPainterPath shape; // what the fill covers and the line follows
    switch (frame.style) {
    case FrameStyle::Rounded:
        shape.addRoundedRect(r, g.radius, g.radius);
        break;
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
    return {
        {QLatin1String("style"), frameStyleKey(style)},
        {QLatin1String("line"), line},
        {QLatin1String("lineColor"), lineColor.name(QColor::HexArgb)},
        {QLatin1String("filled"), filled},
        {QLatin1String("fill"), fill.name(QColor::HexArgb)},
        {QLatin1String("padding"), padding},
    };
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
    const QFontMetricsF metrics(font);
    const int box = std::clamp(settings.boxWidth, 0, TextSettings::MaxBoxWidth);
    if (box > 0) {
        QStringList wrapped;
        for (const QString &line : std::as_const(lines))
            wrapped += wrapLine(line, metrics, double(box));
        lines = wrapped;
    }
    double widest = double(box);
    for (const QString &line : std::as_const(lines))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    const int outline = std::clamp(settings.outline, 0, TextSettings::MaxOutline);
    // Italics and swashes reach past their advance width; an outline reaches
    // further by its own width, on every side.
    const int room = int(std::ceil(metrics.height() * 0.3)) + 1;
    const double lineHeight = metrics.lineSpacing();
    // A frame goes round the block of lines, its padding away, and needs
    // room of its own beyond that for its line and whatever its corners do.
    const bool framed = settings.frame.isActive();
    const double blockHeight = lineHeight * double(lines.size() - 1) + metrics.ascent() + metrics.descent();
    const double padding = framed ? double(std::clamp(settings.frame.padding, 0, TextFrame::MaxPadding)) : 0.0;
    const FrameGeometry geometry = frameGeometry(settings.frame,
                                                 QSizeF(widest + 2.0 * padding, blockHeight + 2.0 * padding));
    const int around = framed ? int(std::ceil(padding + geometry.reach)) : 0;
    const int pad = std::max(room + outline, around);
    const int top = std::max(room / 2 + outline, around);
    const int bottom = std::max(room - room / 2 + outline, around);
    const int w = int(std::ceil(widest)) + 2 * pad;
    const int h = int(std::ceil(lineHeight * double(lines.size()) + metrics.descent())) + top + bottom;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return {};
    const QRectF padded(pad - padding, top - padding, widest + 2.0 * padding, blockHeight + 2.0 * padding);
    if (frame)
        *frame = framed ? padded.adjusted(-geometry.reach, -geometry.reach, geometry.reach, geometry.reach)
                              .toAlignedRect()
                              .intersected(QRect(0, 0, w, h))
                        : QRect();
    if (inset)
        *inset = QPoint(pad, top);
    if (width)
        *width = int(std::ceil(widest));

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, settings.smooth);
    p.setRenderHint(QPainter::TextAntialiasing, settings.smooth);
    p.setFont(font);
    if (framed)
        drawFrame(p, settings.frame, geometry, padded);
    const QColor ink(settings.color.red(), settings.color.green(), settings.color.blue());
    p.setPen(ink);
    QPainterPath letters;
    for (int i = 0; i < lines.size(); ++i) {
        const double advance = metrics.horizontalAdvance(lines.at(i));
        double x = pad;
        if (settings.align & Qt::AlignHCenter)
            x = pad + (widest - advance) / 2.0;
        else if (settings.align & Qt::AlignRight)
            x = pad + (widest - advance);
        const QPointF baseline(x, double(top) + metrics.ascent() + lineHeight * i);
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
    QPoint inset;
    int width = 0;
    QRect frame;
    out.image = renderText(settings, &inset, &width, &frame);
    if (out.image.isNull())
        return out;
    QPoint corner = anchor;
    if (settings.align & Qt::AlignHCenter)
        corner.rx() -= width / 2;
    else if (settings.align & Qt::AlignRight)
        corner.rx() -= width;
    out.origin = corner - inset;
    out.box = frame.isEmpty() ? QRect(corner.x(), out.origin.y(), std::max(width, 1), out.image.height())
                              : frame.translated(out.origin);
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
