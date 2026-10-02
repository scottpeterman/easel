#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
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

    static constexpr int MinSize = 4;
    static constexpr int MaxSize = 2000;
};

// The text as pixels, in the tile format (RGBA16F, linear light,
// premultiplied), transparent around the letters. The image is just big
// enough for the lines, with a little room at the sides for letters that lean
// out of their box. Null when there's nothing to draw.
// inset: where the top-left of the text block sits inside the image.
// width: the width of the longest line.
QImage renderText(const TextSettings &settings, QPoint *inset = nullptr, int *width = nullptr);

// The family actually used for a name: the name itself if that font is
// installed, otherwise what the system substitutes.
QString resolvedFontFamily(const QString &family);

} // namespace easeletch
