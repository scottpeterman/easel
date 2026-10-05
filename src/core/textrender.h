#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>

namespace easeletch {

struct TextSettings {
    QString text;       // may hold several lines
    QString family;     // a font installed on this machine; empty = the default
    int pixelSize = 48; // letter height in canvas pixels
    bool bold = false;
    bool italic = false;
    Qt::Alignment align = Qt::AlignLeft; // of the lines against each other
    // Off draws hard-edged letters with no in-between pixels, for sprites
    // and pixel art.
    bool smooth = true;
    QColor color = Qt::black;
    // A line round the letters, this many pixels wide (0 = none): what keeps
    // white words readable on a photo.
    int outline = 0;
    QColor outlineColor = Qt::black;
    // The width the words wrap inside, in canvas pixels; 0 = lines break only
    // where the text does. Alignment is then against this width.
    int boxWidth = 0;

    static constexpr int MinSize = 4;
    static constexpr int MaxSize = 2000;
    static constexpr int MaxOutline = 200;
    static constexpr int MaxBoxWidth = 16000;

    // As a text layer keeps them in an .easeletch file.
    QJsonObject toJson() const;
    static TextSettings fromJson(const QJsonObject &o);
};

// The text as pixels, in the tile format (RGBA16F, linear light,
// premultiplied), transparent around the letters. The image is just big
// enough for the lines, with a little room at the sides for letters that lean
// out of their box. Null when there's nothing to draw.
// inset: where the top-left of the text block sits inside the image.
// width: the width of the longest line.
QImage renderText(const TextSettings &settings, QPoint *inset = nullptr, int *width = nullptr);

// Text hung from a point: left-aligned text starts there, centred text is
// centred on it, right-aligned text ends there.
struct TextLayout {
    QImage image;  // as renderText() gives it; null when there's nothing to draw
    QPoint origin; // where the image's top-left goes on the canvas
    QRect box;     // the block of text itself, without the image's margins
};
TextLayout layoutText(const TextSettings &settings, const QPoint &anchor);

// The family actually used for a name: the name itself if that font is
// installed, otherwise what the system substitutes.
QString resolvedFontFamily(const QString &family);

} // namespace easeletch
