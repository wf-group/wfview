import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

// RTTY (Baudot FSK): software decoder on the receive audio and AFSK transmit
// through wfview's TX audio. See RttyController.h.
ApplicationWindow {
    id: rttyWindow
    transientParent: null
    title: qsTr("RTTY")
    width: 900
    height: 760
    color: palette.window

    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true

    palette {
        window: _sysTheme ? undefined : MainController.settings.options["Color.Window"]
        windowText: _sysTheme ? undefined : MainController.settings.options["Color.WindowText"]
        base: _sysTheme ? undefined : MainController.settings.options["Color.Base"]
        alternateBase: _sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
        text: _sysTheme ? undefined : MainController.settings.options["Color.MainText"]
        button: _sysTheme ? undefined : MainController.settings.options["Color.Button"]
        buttonText: _sysTheme ? undefined : MainController.settings.options["Color.ButtonText"]
        brightText: _sysTheme ? undefined : MainController.settings.options["Color.BrightText"]
        highlight: _sysTheme ? undefined : MainController.settings.options["Color.Highlight"]
        highlightedText: _sysTheme ? undefined : MainController.settings.options["Color.HighlightedText"]
        mid: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
        dark: _sysTheme ? undefined : MainController.settings.options["Color.Dark"]
        light: _sysTheme ? undefined : MainController.settings.options["Color.Light"]
        placeholderText: _sysTheme ? undefined : MainController.settings.options["Color.PlaceholderText"]
    }

    property var rtty: null
    visible: rtty ? rtty.visible : false

    readonly property var baudRates: [45.45, 50, 75, 100]
    readonly property var shifts: [170, 200, 425, 850]
    readonly property var stopBitChoices: [1, 1.5, 2]

    function indexOfValue(list, v) {
        for (var i = 0; i < list.length; ++i)
            if (Math.abs(list[i] - v) < 0.01)
                return i
        return -1
    }

    // Scrollback limits for the receive pane; trimmed to 90% when exceeded.
    readonly property int maxTextLines: 2000
    readonly property int maxTextChars: 200000

    function appendLimited(area, text) {
        area.insert(area.length, text)
        var t = area.text
        var cut = 0
        if (t.length > maxTextChars)
            cut = t.length - Math.floor(maxTextChars * 0.9)
        var lines = t.split("\n").length
        if (lines > maxTextLines) {
            var drop = lines - Math.floor(maxTextLines * 0.9)
            var pos = -1
            for (var i = 0; i < drop && (pos = t.indexOf("\n", pos + 1)) >= 0; ++i) {}
            if (pos >= 0)
                cut = Math.max(cut, pos + 1)
        }
        if (cut > 0)
            area.remove(0, cut)
        area.cursorPosition = area.length
    }

    function sendTyped() {
        if (!rtty || txInput.text.length === 0)
            return
        rtty.send(txInput.text + "\n")
        txInput.text = ""
    }

    Component.onCompleted: {
        closing.connect(function(close) {
            if (rtty)
                rtty.visible = false
            close.accepted = true
        })
    }

    Connections {
        target: rttyWindow.rtty
        function onTextAppended(text) {
            rttyWindow.appendLimited(rxText, text)
        }
        function onScopePoints(xy) {
            // keep at most a few frames if painting falls behind
            var p = scope.pending.concat(xy)
            scope.pending = p.length > 3200 ? p.slice(p.length - 3200) : p
        }
    }

    Shortcut { sequence: "F1"; onActivated: if (rtty) rtty.sendMacro(0) }
    Shortcut { sequence: "F2"; onActivated: if (rtty) rtty.sendMacro(1) }
    Shortcut { sequence: "F3"; onActivated: if (rtty) rtty.sendMacro(2) }
    Shortcut { sequence: "F4"; onActivated: if (rtty) rtty.sendMacro(3) }
    Shortcut { sequence: "F5"; onActivated: if (rtty) rtty.sendMacro(4) }
    Shortcut { sequence: "F6"; onActivated: if (rtty) rtty.sendMacro(5) }
    Shortcut { sequence: "F7"; onActivated: if (rtty) rtty.sendMacro(6) }
    Shortcut { sequence: "F8"; onActivated: if (rtty) rtty.sendMacro(7) }
    Shortcut { sequence: "Esc"; onActivated: if (rtty) rtty.abortTx() }

    // Horizontal level bar, 0..1. Inline components cannot see this window's
    // ids, so the colours are passed in where the bars are used.
    component LevelBar: Rectangle {
        property real fraction: 0
        property color fill: "#4a8fd0"
        property color back: "#202020"
        property color edge: "#606060"
        implicitHeight: 14
        implicitWidth: 140
        color: back
        border.color: edge
        Rectangle {
            x: 1
            y: 1
            height: parent.height - 2
            width: Math.max(0, Math.min(1, parent.fraction)) * (parent.width - 2)
            color: parent.fill
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // ---- controls (left) and tuning scope (right) -----------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                // ---- receive controls ------------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    CheckBox {
                        text: qsTr("Decode")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Decode RTTY from the receive audio (after NR, before RX mute).")
                        checked: rtty ? rtty.decoding : false
                        onToggled: if (rtty) rtty.decoding = checked
                    }

                    Label { text: qsTr("Squelch:") }
                    Slider {
                        id: squelchSlider
                        Layout.preferredWidth: 140
                        from: 0
                        to: 0.8
                        stepSize: 0.02
                        value: rtty ? rtty.squelch : 0.5
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Print only while the signal quality is above this level.\n"
                                           + "Band noise sits near 0.4; a usable signal is 0.5 or more. Far left = off.")
                        onMoved: if (rtty) rtty.squelch = value
                    }
                    Label {
                        text: rtty && rtty.squelch > 0 ? rtty.squelch.toFixed(2) : qsTr("off")
                        Layout.preferredWidth: 32
                    }

                    Button {
                        text: qsTr("Auto-tune")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Find the strongest tone pair one shift apart in the last few seconds\n"
                                           + "and set the mark tone to it (needs Decode on).")
                        onClicked: if (rtty) rtty.autoTune()
                    }
                    Button {
                        text: qsTr("Re-sync")
                        onClicked: if (rtty) rtty.resync()
                    }
                    Button {
                        text: qsTr("Clear")
                        onClicked: rxText.text = ""
                    }
                    CheckBox {
                        text: qsTr("Reverse")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Swap mark and space (for an upside-down station).")
                        checked: rtty ? rtty.reverse : false
                        onToggled: if (rtty) rtty.reverse = checked
                    }
                    Item { Layout.fillWidth: true }
                }

                // ---- tones and format ------------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label { text: qsTr("Baud:") }
                    ComboBox {
                        model: ["45.45", "50", "75", "100"]
                        implicitContentWidthPolicy: ComboBox.WidestText
                        currentIndex: rtty ? Math.max(0, rttyWindow.indexOfValue(rttyWindow.baudRates, rtty.baud)) : 0
                        onActivated: function(index) { if (rtty) rtty.baud = rttyWindow.baudRates[index] }
                    }

                    Label { text: qsTr("Shift:") }
                    ComboBox {
                        model: ["170", "200", "425", "850"]
                        implicitContentWidthPolicy: ComboBox.WidestText
                        currentIndex: rtty ? Math.max(0, rttyWindow.indexOfValue(rttyWindow.shifts, rtty.shift)) : 0
                        onActivated: function(index) { if (rtty) rtty.shift = rttyWindow.shifts[index] }
                    }

                    Label { text: qsTr("Mark:") }
                    SpinBox {
                        id: markSpin
                        from: 300
                        to: 3000
                        stepSize: 5
                        editable: true
                        value: rtty ? rtty.markHz : 2295
                        textFromValue: function(v) { return v + " Hz" }
                        valueFromText: function(t) { return parseInt(t) }
                        onValueModified: if (rtty) rtty.markHz = value
                    }
                    Label {
                        text: rtty ? qsTr("Space: %1 Hz").arg(rtty.spaceHz) : ""
                        color: rttyWindow.palette.windowText
                    }

                    Label { text: qsTr("Sideband:") }
                    ComboBox {
                        model: ["USB", "LSB"]
                        implicitContentWidthPolicy: ComboBox.WidestText
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("The radio's sideband (USB-D / LSB-D). It decides which audio tone is mark,\n"
                                           + "so the RF signal has mark above space as usual.")
                        currentIndex: rtty && !rtty.usb ? 1 : 0
                        onActivated: function(index) { if (rtty) rtty.usb = (index === 0) }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label { text: qsTr("Stop bits:") }
                    ComboBox {
                        model: ["1", "1.5", "2"]
                        implicitContentWidthPolicy: ComboBox.WidestText
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("For transmit. The decoder accepts any stop length.")
                        currentIndex: rtty ? Math.max(0, rttyWindow.indexOfValue(rttyWindow.stopBitChoices, rtty.stopBits)) : 1
                        onActivated: function(index) { if (rtty) rtty.stopBits = rttyWindow.stopBitChoices[index] }
                    }
                    CheckBox {
                        text: qsTr("USOS")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Unshift on space: back to letters after every space (receive and transmit).")
                        checked: rtty ? rtty.usos : true
                        onToggled: if (rtty) rtty.usos = checked
                    }
                    Label { text: qsTr("Figures:") }
                    ComboBox {
                        model: [qsTr("US TTY"), qsTr("ITA2")]
                        implicitContentWidthPolicy: ComboBox.WidestText
                        currentIndex: rtty && rtty.ita2 ? 1 : 0
                        onActivated: function(index) { if (rtty) rtty.ita2 = (index === 1) }
                    }
                    Label { text: qsTr("My call:") }
                    TextField {
                        Layout.preferredWidth: 110
                        text: rtty ? rtty.myCall : ""
                        placeholderText: qsTr("for {MYCALL}")
                        onEditingFinished: if (rtty) rtty.myCall = text
                    }
                    Item { Layout.fillWidth: true }
                }

                // ---- tuning indicator ------------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label { text: qsTr("Mark") }
                    LevelBar {
                        fraction: rtty ? rtty.markDb / 40 : 0
                        fill: rttyWindow.palette.highlight
                        back: rttyWindow.palette.base
                        edge: rttyWindow.palette.mid
                    }
                    Label { text: qsTr("Space") }
                    LevelBar {
                        fraction: rtty ? rtty.spaceDb / 40 : 0
                        fill: rttyWindow.palette.highlight
                        back: rttyWindow.palette.base
                        edge: rttyWindow.palette.mid
                    }
                    Label { text: qsTr("Quality") }
                    LevelBar {
                        fraction: rtty ? rtty.quality : 0
                        fill: rtty && rtty.squelchOpen ? "#3a9a3a" : rttyWindow.palette.mid
                        back: rttyWindow.palette.base
                        edge: rttyWindow.palette.mid
                    }
                    Label {
                        text: rtty ? rtty.quality.toFixed(2) : ""
                        Layout.preferredWidth: 32
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // MMTTY-style crossed-ellipse tuning scope: X = mark filter,
            // Y = space filter. A tuned signal draws a clean "+".
            Rectangle {
                id: scopeFrame
                Layout.fillHeight: true
                Layout.preferredWidth: height
                Layout.minimumWidth: 110
                color: "black"
                border.color: "#1f5f2f"
                radius: 3
                ToolTip.visible: scopeHover.containsMouse
                ToolTip.text: qsTr("Tuning scope: mark tone = horizontal line, space tone = vertical line.\n"
                                   + "Well tuned: a clean +. Tilted lines or ellipses: off frequency or wrong shift.\n"
                                   + "Noise only: a round blob.")

                MouseArea {
                    id: scopeHover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }

                Canvas {
                    id: scope
                    anchors.fill: parent
                    anchors.margins: 3
                    property var pending: []
                    property bool wipe: true
                    readonly property bool live: rttyWindow.visible && rtty !== null && rtty.decoding && !rtty.transmitting
                    onLiveChanged: { wipe = true; requestPaint() }

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        if (wipe) {
                            ctx.fillStyle = "black"
                            ctx.fillRect(0, 0, w, h)
                            wipe = false
                        } else {
                            // phosphor decay: each frame dims what is already on the screen
                            ctx.fillStyle = "rgba(0, 0, 0, 0.28)"
                            ctx.fillRect(0, 0, w, h)
                        }
                        ctx.strokeStyle = "rgba(40, 140, 70, 0.30)"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(w / 2, 0); ctx.lineTo(w / 2, h)
                        ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2)
                        ctx.stroke()

                        var p = pending
                        pending = []
                        var cx = w / 2, cy = h / 2
                        var r = Math.min(w, h) / 2 - 3
                        ctx.fillStyle = "rgba(40, 255, 100, 0.22)" // glow
                        for (var i = 0; i + 1 < p.length; i += 2)
                            ctx.fillRect(cx + p[i] * r - 1.5, cy - p[i + 1] * r - 1.5, 3, 3)
                        ctx.fillStyle = "#8dffa8" // bright core
                        for (var j = 0; j + 1 < p.length; j += 2)
                            ctx.fillRect(cx + p[j] * r - 0.5, cy - p[j + 1] * r - 0.5, 1.2, 1.2)
                    }

                    Timer {
                        interval: 66
                        repeat: true
                        running: scope.live
                        onTriggered: scope.requestPaint()
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: !scope.live
                    text: rtty && rtty.transmitting ? qsTr("TX") : qsTr("Decode off")
                    color: "#2f7f45"
                }
            }
        }

        // ---- receive text (also shows [TX] echo) -----------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            TextArea {
                id: rxText
                readOnly: true
                textFormat: TextEdit.PlainText
                wrapMode: TextArea.Wrap
                font.family: "Courier"
                font.pixelSize: 13
                placeholderText: qsTr("Decoded RTTY appears here. Tick Decode, tune the signal into the passband, press Auto-tune.")
                color: rttyWindow.palette.text
                background: Rectangle { color: rttyWindow.palette.base }
            }
        }

        Label {
            Layout.fillWidth: true
            text: rtty ? rtty.status : ""
            elide: Text.ElideRight
            color: rttyWindow.palette.windowText
        }

        // ---- macros ------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Label { text: qsTr("Call:") }
            TextField {
                Layout.preferredWidth: 110
                text: rtty ? rtty.call : ""
                placeholderText: qsTr("for {CALL}")
                onEditingFinished: if (rtty) rtty.call = text
            }

            Repeater {
                model: rtty ? rtty.macroNames : []
                delegate: Button {
                    required property int index
                    required property var modelData
                    text: "F" + (index + 1) + " " + modelData
                    enabled: rtty ? rtty.canTransmit : false
                    ToolTip.visible: hovered
                    ToolTip.text: rtty ? rtty.macroTexts[index] : ""
                    onClicked: if (rtty) rtty.sendMacro(index)
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                id: editMacrosButton
                text: checked ? qsTr("Done") : qsTr("Edit macros")
                checkable: true
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: editMacrosButton.checked
            columns: 3
            columnSpacing: 6
            rowSpacing: 3

            Repeater {
                model: editMacrosButton.checked && rtty ? rtty.macroNames.length : 0
                delegate: RowLayout {
                    id: macroRow
                    required property int index
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    spacing: 6

                    function store() {
                        if (rtty)
                            rtty.setMacro(macroRow.index, nameField.text, textField.text.replace(/\\n/g, "\n"))
                    }

                    Label { text: "F" + (macroRow.index + 1) }
                    TextField {
                        id: nameField
                        Layout.preferredWidth: 100
                        text: rtty ? rtty.macroNames[macroRow.index] : ""
                        onEditingFinished: macroRow.store()
                    }
                    TextField {
                        id: textField
                        Layout.fillWidth: true
                        font.family: "Courier"
                        // A line break is shown and typed as \n.
                        text: rtty ? rtty.macroTexts[macroRow.index].replace(/\n/g, "\\n") : ""
                        onEditingFinished: macroRow.store()
                    }
                }
            }
            Label {
                Layout.columnSpan: 3
                text: qsTr("{MYCALL} and {CALL} are replaced by the two call fields; \\n is a new line. F1-F8 send, Esc aborts.")
                color: rttyWindow.palette.windowText
                font.italic: true
            }
        }

        // ---- transmit --------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            TextField {
                id: txInput
                Layout.fillWidth: true
                enabled: rtty ? rtty.canTransmit : false
                font.family: "Courier"
                font.pixelSize: 13
                font.capitalization: Font.AllUppercase
                placeholderText: qsTr("Type here, Enter to send (keys the radio). Radio in USB-D/LSB-D.")
                Keys.onReturnPressed: rttyWindow.sendTyped()
                Keys.onEnterPressed: rttyWindow.sendTyped()
            }
            Button {
                text: qsTr("Send")
                enabled: rtty ? rtty.canTransmit : false
                onClicked: rttyWindow.sendTyped()
            }
            Button {
                text: qsTr("TX")
                enabled: rtty ? rtty.canTransmit && !rtty.transmitting : false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Key up and send idle (mark or diddles) until RX is pressed.")
                onClicked: if (rtty) rtty.startTx()
            }
            Button {
                text: qsTr("RX")
                enabled: rtty ? rtty.transmitting : false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Unkey after the text already typed has gone out.")
                onClicked: if (rtty) rtty.finishTx()
            }
            Button {
                text: qsTr("Abort")
                enabled: rtty ? rtty.transmitting : false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Unkey now (Esc).")
                onClicked: if (rtty) rtty.abortTx()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                implicitWidth: 34
                implicitHeight: 18
                radius: 3
                color: rtty && rtty.transmitting ? "#c03030" : rttyWindow.palette.mid
                Label {
                    anchors.centerIn: parent
                    text: "TX"
                    color: "white"
                    font.bold: true
                }
            }
            CheckBox {
                text: qsTr("Hold TX")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Stay keyed between lines (otherwise the radio unkeys when the text has gone out).")
                checked: rtty ? rtty.holdTx : false
                onToggled: if (rtty) rtty.holdTx = checked
            }
            CheckBox {
                text: qsTr("Diddle")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("While keyed and waiting for text, send LTRS instead of steady mark.")
                checked: rtty ? rtty.diddle : false
                onToggled: if (rtty) rtty.diddle = checked
            }
            Label { text: qsTr("TX level:") }
            Slider {
                Layout.preferredWidth: 160
                from: 0.05
                to: 1.0
                stepSize: 0.05
                value: rtty ? rtty.txLevel : 0.5
                ToolTip.visible: hovered
                ToolTip.text: qsTr("AFSK level into the radio. Set it for no ALC with the radio's USB/LAN MOD level.")
                onMoved: if (rtty) rtty.txLevel = value
            }
            Label { text: rtty ? Math.round(rtty.txLevel * 100) + " %" : "" }
            Item { Layout.fillWidth: true }
        }
    }
}
