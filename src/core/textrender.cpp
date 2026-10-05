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

QString alignKey(Qt::Alignment align)
{
    return QLatin1String(align & Qt::AlignHCenter ? "centre" : align & Qt::AlignRight ? "right" : "left");
}

} // namespace

QJsonObject TextSettings::toJson() const
{
    return {
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
    return s;
}

QImage renderText(const TextSettings &settings, QPoint *inset, int *width)
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
    const int pad = room + outline;
    const int top = room / 2 + outline;
    const double lineHeight = metrics.lineSpacing();
    const int w = int(std::ceil(widest)) + 2 * pad;
    const int h = int(std::ceil(lineHeight * double(lines.size()) + metrics.descent())) + room + 2 * outline;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return {};
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
                else if (outline > 0)
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
    out.image = renderText(settings, &inset, &width);
    if (out.image.isNull())
        return out;
    QPoint corner = anchor;
    if (settings.align & Qt::AlignHCenter)
        corner.rx() -= width / 2;
    else if (settings.align & Qt::AlignRight)
        corner.rx() -= width;
    out.origin = corner - inset;
    out.box = QRect(corner.x(), out.origin.y(), std::max(width, 1), out.image.height());
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
