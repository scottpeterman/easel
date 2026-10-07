#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class ColorWheel;
class QGridLayout;
class QHBoxLayout;
class QLineEdit;
class QSettings;
class SwatchButton;

// The painting colour: a hue ring and saturation/value square, hex entry, the
// colours you've recently painted with, and one saved palette.
// Colours are opaque; transparency is the brush's opacity.
class ColorPanel : public QWidget
{
    Q_OBJECT

public:
    static constexpr int MaxRecent = 12;
    static constexpr int PaletteColumns = 8;
    static constexpr int RecentSwatch = 18;

    explicit ColorPanel(QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    QList<QColor> recentColors() const { return m_recent; }
    QList<QColor> savedColors() const { return m_saved; }
    void setSavedColors(const QList<QColor> &colors);

    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

public slots:
    void setColor(const QColor &color);
    // Moves a colour to the front of the recent row (after it's painted with).
    void noteUsed(const QColor &color);
    void addToPalette(const QColor &color);
    void replaceInPalette(int index, const QColor &color);
    void removeFromPalette(int index);

signals:
    void colorChanged(const QColor &color);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyHex();
    void rebuildRecent();
    void rebuildPalette();
    void showPaletteMenu(int index, const QPoint &globalPos);

    QColor m_color = Qt::black;
    QList<QColor> m_recent;
    QList<QColor> m_saved;

    ColorWheel *m_wheel = nullptr;
    SwatchButton *m_current = nullptr;
    QLineEdit *m_hex = nullptr;
    QHBoxLayout *m_recentRow = nullptr;
    QGridLayout *m_paletteGrid = nullptr;
};
