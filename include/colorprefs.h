#ifndef COLORPREFS_H
#define COLORPREFS_H

#include <QObject>
#include <QColor>
#include <QString>

#define numColorPresetsTotal (5)

/*
QPalette::Window           // General background color
QPalette::WindowText       // General foreground color (text on Window)
QPalette::Base             // Background for text entry widgets, item views
QPalette::AlternateBase    // Alternate background (e.g., alternating table rows)
QPalette::Text             // Foreground for Base (text in entry fields)
QPalette::Button           // Button background
QPalette::ButtonText       // Button text/foreground
QPalette::BrightText       // Very bright text (for contrast)

QPalette::Light            // Lighter than Button color
QPalette::Midlight         // Between Button and Light
QPalette::Dark             // Darker than Button color
QPalette::Mid              // Between Button and Dark
QPalette::Shadow           // Very dark color (for shadows)

QPalette::Highlight        // Selected/highlighted item background
QPalette::HighlightedText  // Selected/highlighted item text

QPalette::Link             // Unvisited hyperlink color
QPalette::LinkVisited      // Visited hyperlink color

QPalette::ToolTipBase      // Tooltip background
QPalette::ToolTipText      // Tooltip text

QPalette::PlaceholderText  // Placeholder text in input fields (Qt 5.12+)

// Obsolete but still available:
QPalette::Foreground       // Deprecated, use WindowText
QPalette::Background       // Deprecated, use Window

*/
struct colorPrefsType{
    int presetNum = -1;
    QString *presetName = nullptr;

    QColor window;
    QColor windowText;
    QColor base;
    QColor alternateBase;
    QColor mainText;
    QColor button;
    QColor buttonText;
    QColor brightText;
    QColor light;
    QColor midLight;
    QColor dark;
    QColor mid;
    QColor shadow;
    QColor highlight;
    QColor highlightedText;
    QColor link;
    QColor linkVisited;
    QColor toolTipBase;
    QColor toolTipText;
    QColor placeholder;

    // Spectrum line plot:
    QColor gridColor;
    QColor axisColor;
    QColor textColor;
    QColor spectrumLine;
    QColor spectrumFill;
    QColor spectrumFillTop;
    QColor spectrumFillBot;
    bool useSpectrumFillGradient = false;
    QColor underlayLine;
    QColor underlayFill;
    bool useUnderlayFillGradient = false;
    QColor underlayFillTop;
    QColor underlayFillBot;
    QColor plotBackground;
    QColor tuningLine;
    QColor passband;
    QColor pbt;

    // Waterfall:
    QColor wfBackground;
    QColor wfGrid;
    QColor wfAxis;
    QColor wfText;

    // Meters:
    QColor meterLevel;
    QColor meterAverage;
    QColor meterPeakLevel;
    QColor meterPeakScale;
    QColor meterScale;
    QColor meterLowerLine;
    QColor meterLowText;

    // Assorted
    QColor clusterSpots;
    QColor buttonOff;
    QColor buttonOn;

};

Q_DECLARE_METATYPE(colorPrefsType)


#endif // COLORPREFS_H
