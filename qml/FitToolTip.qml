import QtQuick 2.15
import QtQuick.Controls 2.15

// Tooltip sized to its text. Fusion's tooltip wraps its text, which can size the
// box far wider than short lines (or lines split with \n) need, so lay the text
// out unwrapped instead.
ToolTip {
    id: fitToolTip
    contentItem: Text {
        text: fitToolTip.text
        font: fitToolTip.font
        color: fitToolTip.palette.toolTipText
        wrapMode: Text.NoWrap
    }
}
