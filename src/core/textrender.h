#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>

namespace easeletch {

// A frame drawn round a block of text: what makes a caption a card. It sits
// outside the words by its padding, so adding one doesn't move them.
enum class FrameStyle {
    None,
    Single,  // one line
    Double,  // a heavy line with a fine one inside it
    Rounded, // one line, round corners
    Corners, // marks at the four corners only
    Notched, // corners bitten out in a quarter circle
    Looped,  // the line curls round in a loop at each corner
};
inline constexpr int FrameStyleCount = 7;

// Name used in .easeletch files ("single", "double", ...); "" for None.
QString frameStyleKey(FrameStyle style);
FrameStyle frameStyleFromKey(const QString &key);

struct TextFrame {
    FrameStyle style = FrameStyle::None;
    int line = 3;      // line width in canvas pixels
    QColor lineColor = Qt::black;
    bool filled = false; // a flat colour behind the words, inside the frame
    QColor fill = Qt::white;
    int padding = 16;  // space between the words and the frame

    static constexpr int MinLine = 1;
    static constexpr int MaxLine = 100;
    static constexpr int MaxPadding = 1000;

    bool isActive() const { return style != FrameStyle::None; }
    QJsonObject toJson() const;
    static TextFrame fromJson(const QJsonObject &o);
};

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
    TextFrame frame;

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
// frame: the area the frame takes up inside the image, decoration included;
// empty when there's no frame.
QImage renderText(const TextSettings &settings, QPoint *inset = nullptr, int *width = nullptr,
                  QRect *frame = nullptr);

// Text hung from a point: left-aligned text starts there, centred text is
// centred on it, right-aligned text ends there.
struct TextLayout {
    QImage image;  // as renderText() gives it; null when there's nothing to draw
    QPoint origin; // where the image's top-left goes on the canvas
    QRect box;     // the block of text itself (with its frame), without the image's margins
};
TextLayout layoutText(const TextSettings &settings, const QPoint &anchor);

// The family actually used for a name: the name itself if that font is
// installed, otherwise what the system substitutes.
QString resolvedFontFamily(const QString &family);

} // namespace easeletch
