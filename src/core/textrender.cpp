#include "textrender.h"

#include "regionops.h"
#include "tilestore.h"

#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QPainter>
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

} // namespace

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
    double widest = 0.0;
    for (const QString &line : std::as_const(lines))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    // Italics and swashes reach past their advance width.
    const int pad = int(std::ceil(metrics.height() * 0.3)) + 1;
    const double lineHeight = metrics.lineSpacing();
    const int w = int(std::ceil(widest)) + 2 * pad;
    const int h = int(std::ceil(lineHeight * double(lines.size()) + metrics.descent())) + pad;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return {};
    if (inset)
        *inset = QPoint(pad, pad / 2);
    if (width)
        *width = int(std::ceil(widest));

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, settings.smooth);
    p.setRenderHint(QPainter::TextAntialiasing, settings.smooth);
    p.setFont(font);
    p.setPen(QColor(settings.color.red(), settings.color.green(), settings.color.blue()));
    for (int i = 0; i < lines.size(); ++i) {
        const double advance = metrics.horizontalAdvance(lines.at(i));
        double x = pad;
        if (settings.align & Qt::AlignHCenter)
            x = pad + (widest - advance) / 2.0;
        else if (settings.align & Qt::AlignRight)
            x = pad + (widest - advance);
        p.drawText(QPointF(x, double(pad / 2) + metrics.ascent() + lineHeight * i), lines.at(i));
    }
    p.end();
    if (!settings.smooth) {
        // No in-between pixels at all: a letter pixel is there or it isn't.
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                row[x] = qAlpha(row[x]) >= 128 ? settings.color.rgb() | 0xff000000u : 0u;
        }
    }
    // Coverage becomes alpha as it is; the colour goes to linear light.
    return fromClipboardImage(image);
}

QString resolvedFontFamily(const QString &family)
{
    QFont f;
    if (!family.isEmpty())
        f.setFamily(family);
    return QFontInfo(f).family();
}

} // namespace easeletch
