#pragma once

#include "tilestore.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

namespace easeletch {

// A sheet to draw on: its colour and what its surface looks like.
struct PaperStyle {
    QString id;   // stable, for settings
    QString name; // as shown
    QColor base;
    // How strongly each shows, as a fraction of full brightness; 0 is none.
    double tooth = 0.0;     // the fine bumps of the surface
    double toothSize = 2.0; // across one bump, in canvas pixels
    double fibres = 0.0;    // short streaks, as in cotton rag or kraft
    double mottle = 0.0;    // broad uneven patches, as in parchment
};

// Built in: cotton, cold press, cream, parchment, sepia, kraft, grey, black.
const QList<PaperStyle> &paperStyles();
// Null if there's no such paper.
const PaperStyle *paperStyle(const QString &id);

// The paper's colour at a canvas pixel, as sRGB. It belongs to the canvas:
// the same place is the same every time.
QColor paperColorAt(const PaperStyle &style, int x, int y);

// Covers the canvas with the paper. What the store reads as outside the
// canvas becomes the paper's plain colour.
void fillPaper(TileStore &store, const QRect &canvas, const PaperStyle &style);

// A patch of it at full size, for a list to choose from.
QImage paperPreview(const PaperStyle &style, const QSize &size);

} // namespace easeletch
