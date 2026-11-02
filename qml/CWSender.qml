import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: cwSenderWindow
    transientParent: null
    title: qsTr("CW Sender")
    width: 835
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

    property var cwSenderController: null
    // Software CW decoder (decodes the receive audio; works with any radio).
    readonly property var audioDecoder: cwSenderController ? cwSenderController.audioDecoder : null
    
    visible: cwSenderController ? cwSenderController.visible : false

    function applyDisabledPalette() {
        if (_sysTheme) return
        try {
            palette.disabled.window = MainController.settings.options["Color.Window"]
            palette.disabled.windowText = Qt.darker(MainController.settings.options["Color.WindowText"], 2.5)
            palette.disabled.base = Qt.darker(MainController.settings.options["Color.Base"], 1.1)
            palette.disabled.alternateBase = Qt.darker(MainController.settings.options["Color.AlternateBase"], 1.1)
            palette.disabled.text = Qt.darker(MainController.settings.options["Color.MainText"], 2.5)
            palette.disabled.button = Qt.darker(MainController.settings.options["Color.Button"], 1.3)
            palette.disabled.buttonText = Qt.darker(MainController.settings.options["Color.ButtonText"], 2.5)
            palette.disabled.brightText = Qt.darker(MainController.settings.options["Color.BrightText"], 2.5)
            palette.disabled.highlight = Qt.darker(MainController.settings.options["Color.Highlight"], 1.5)
            palette.disabled.highlightedText = Qt.darker(MainController.settings.options["Color.MainText"], 2.5)
            palette.disabled.mid = MainController.settings.options["Color.Mid"]
            palette.disabled.dark = MainController.settings.options["Color.Dark"]
            palette.disabled.light = Qt.darker(MainController.settings.options["Color.Light"], 1.2)
            palette.disabled.placeholderText = Qt.darker(MainController.settings.options["Color.PlaceholderText"], 1.5)
        } catch (e) {
            // Qt 5 does not expose palette disabled groups to QML.
        }
    }
    
    // Scrollback limits for the receive and transcript panes. When either
    // limit is exceeded the oldest text is trimmed back to 90% of the limit.
    readonly property int maxTextLines: 2000
    readonly property int maxTextChars: 200000

    // Text arrives a few characters at a time (decoded CW, immediate-send
    // words), so insert inline rather than as a new paragraph per call.
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

    Component.onCompleted: {
        applyDisabledPalette()
        closing.connect(function(close) {
            if (cwSenderController) {
                cwSenderController.visible = false
            }
            close.accepted = true
        })
    }
    
    Connections {
        target: cwSenderController
        
        function onReceiveTextAppended(text) {
            cwSenderWindow.appendLimited(receiveText, text)
        }
        
        function onTranscriptTextAppended(text) {
            cwSenderWindow.appendLimited(transcriptText, text)
        }
        
        function onTextColorChanged(color) {
            textToSendEdit.color = color || "white"
        }
        
        function onClearTextInput() {
            textToSendEdit.text = ""
        }
        
        function onMacroButtonTextChanged(index, text) {
            // Update macro button text
            if (macroRepeater.itemAt(index - 1)) {
                macroRepeater.itemAt(index - 1).text = text || ("M" + index)
            }
        }
        
        function onStatusMessageChanged() {
            statusBar.text = cwSenderController.statusMessage
        }
    }

    Connections {
        target: cwSenderWindow.audioDecoder

        function onTextAppended(text) {
            cwSenderWindow.appendLimited(decodeText, text)
        }
    }

    Connections {
        target: MainController.settings
        function onColChanged(items) {
            cwSenderWindow.applyDisabledPalette()
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        
        // Software CW decoder controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            CheckBox {
                text: qsTr("Decode")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Decode CW from the receive audio (after NR, before the sidetone).")
                checked: audioDecoder ? audioDecoder.enabled : false
                onToggled: {
                    if (audioDecoder) {
                        audioDecoder.enabled = checked
                    }
                }
            }

            CheckBox {
                text: qsTr("Squelch")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Print nothing while no steady CW tone is present\n(stops random E/T/I letters on band noise).")
                checked: audioDecoder ? audioDecoder.squelch : true
                onToggled: {
                    if (audioDecoder) {
                        audioDecoder.squelch = checked
                    }
                }
            }

            Label { text: qsTr("Decoder:") }
            ComboBox {
                model: [qsTr("Machine (keyer)"), qsTr("Hand key")]
                implicitContentWidthPolicy: ComboBox.WidestText
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Machine: best on keyer or computer-sent CW with even timing.\n"
                                   + "Hand key: learns each sender's own dit, dah and spacing and copies\n"
                                   + "uneven straight-key or bug sending. Prints a word at a time,\n"
                                   + "about a second behind.")
                currentIndex: audioDecoder ? audioDecoder.engine : 0
                onActivated: function(index) {
                    if (audioDecoder) {
                        audioDecoder.engine = index
                    }
                }
            }

            CheckBox {
                text: qsTr("Multi-signal")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Also decode every other CW signal in the receive passband (widen the\n"
                                   + "radio's filter to see more). One line per station; double-click a line\n"
                                   + "to make it the main decode.")
                checked: audioDecoder ? audioDecoder.multiSignal : false
                onToggled: {
                    if (audioDecoder) {
                        audioDecoder.multiSignal = checked
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Re-sync")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restart pitch and speed tracking (e.g. after changing stations).")
                onClicked: {
                    if (audioDecoder) {
                        audioDecoder.resync()
                    }
                }
            }

            Button {
                text: qsTr("Clear")
                onClicked: decodeText.clear()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label { text: qsTr("Decode pitch:") }
            ComboBox {
                id: decodePitchMode
                model: [qsTr("Follow radio"), qsTr("Auto"), qsTr("Fixed")]
                implicitContentWidthPolicy: ComboBox.WidestText
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Follow radio: track the strongest tone within 150 Hz of the radio's CW pitch.\n"
                                   + "Auto: track the strongest steady tone from 200 to 1200 Hz.\n"
                                   + "Fixed: decode exactly at the pitch at right (200 to 2000 Hz).")
                currentIndex: audioDecoder ? audioDecoder.pitchMode : 0
                onActivated: function(index) {
                    if (audioDecoder) {
                        audioDecoder.pitchMode = index
                    }
                }
            }
            SpinBox {
                from: audioDecoder ? audioDecoder.pitchMin : 200
                to: audioDecoder ? audioDecoder.pitchMax : 1200
                stepSize: 10
                editable: true
                inputMethodHints: Qt.ImhDigitsOnly
                // Plain digits ("1250", not "1,250") so typed values are accepted.
                textFromValue: function(value, locale) {
                    return value.toString()
                }
                valueFromText: function(text, locale) {
                    return parseInt(text, 10)
                }
                enabled: decodePitchMode.currentIndex === CwAudioDecoder.PitchFixed
                value: audioDecoder ? audioDecoder.fixedPitch : 600
                onValueModified: {
                    if (audioDecoder) {
                        audioDecoder.fixedPitch = value
                    }
                }
            }

            Label { text: qsTr("Decode speed:") }
            ComboBox {
                id: decodeSpeedMode
                model: [qsTr("Auto"), qsTr("Fixed")]
                implicitContentWidthPolicy: ComboBox.WidestText
                currentIndex: audioDecoder && audioDecoder.fixedSpeed ? 1 : 0
                onActivated: function(index) {
                    if (audioDecoder) {
                        audioDecoder.fixedSpeed = index === 1
                    }
                }
            }
            SpinBox {
                from: 5
                to: 60
                editable: true
                inputMethodHints: Qt.ImhDigitsOnly
                enabled: decodeSpeedMode.currentIndex === 1
                value: audioDecoder ? audioDecoder.speedWpm : 20
                onValueModified: {
                    if (audioDecoder) {
                        audioDecoder.speedWpm = value
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // Decoded text above, Receive/Transcript below; drag the handle to resize.
        SplitView {
            id: paneSplitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            handle: Rectangle {
                implicitHeight: 6
                color: SplitHandle.pressed ? cwSenderWindow.palette.highlight
                     : SplitHandle.hovered ? cwSenderWindow.palette.mid
                     : cwSenderWindow.palette.window
            }

            // Software decoder output, with the multi-signal list at right when enabled.
            SplitView {
                id: decodeSplitView
                SplitView.preferredHeight: paneSplitView.height * 0.55
                SplitView.minimumHeight: 80
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.pressed ? cwSenderWindow.palette.highlight
                         : SplitHandle.hovered ? cwSenderWindow.palette.mid
                         : cwSenderWindow.palette.window
                }

                ScrollView {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 100
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: decodeText
                        readOnly: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextArea.Wrap
                        font.family: "Courier"
                        font.pixelSize: 14
                        placeholderText: qsTr("Decoded CW (tick Decode)")
                        color: cwSenderWindow.palette.text
                        background: Rectangle { color: cwSenderWindow.palette.base }
                    }
                }

                ListView {
                    id: multiSignalList
                    visible: audioDecoder ? audioDecoder.multiSignal : false
                    SplitView.preferredWidth: decodeSplitView.width * 0.4
                    SplitView.minimumWidth: 150
                    clip: true
                    model: audioDecoder ? audioDecoder.channels : []
                    ScrollBar.vertical: ScrollBar {}

                    header: RowLayout {
                        width: multiSignalList.width
                        spacing: 8

                        Label {
                            Layout.leftMargin: 4
                            Layout.preferredWidth: pitchColumn.width
                            text: qsTr("Pitch")
                            font: pitchColumn.font
                        }
                        Label {
                            Layout.preferredWidth: wpmColumn.width
                            text: qsTr("WPM")
                            font: pitchColumn.font
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Decoded (double-click to decode)")
                            font: pitchColumn.font
                            elide: Text.ElideRight
                        }
                    }

                    // Widths of the fixed pitch and WPM columns.
                    TextMetrics {
                        id: pitchColumn
                        font.family: "Courier"
                        font.pixelSize: 12
                        text: "0000 Hz"
                    }
                    TextMetrics {
                        id: wpmColumn
                        font: pitchColumn.font
                        text: "WPM"
                    }

                    delegate: Rectangle {
                        id: channelRow
                        required property var modelData
                        width: multiSignalList.width
                        implicitHeight: channelCells.implicitHeight + 6
                        color: cwSenderWindow.palette.base
                        // Stations that have gone quiet stay listed, dimmed, for a while.
                        readonly property color textColor: cwSenderWindow.palette.text
                        opacity: modelData.active ? 1.0 : 0.55

                        RowLayout {
                            id: channelCells
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            spacing: 8

                            Label {
                                Layout.preferredWidth: pitchColumn.width
                                horizontalAlignment: Text.AlignRight
                                text: Math.round(channelRow.modelData.pitch) + " Hz"
                                font: pitchColumn.font
                                color: channelRow.textColor
                            }
                            Label {
                                Layout.preferredWidth: wpmColumn.width
                                horizontalAlignment: Text.AlignRight
                                text: channelRow.modelData.wpm > 0 ? Math.round(channelRow.modelData.wpm) : "--"
                                font: pitchColumn.font
                                color: channelRow.textColor
                            }
                            Label {
                                Layout.fillWidth: true
                                // The newest text stays visible; the oldest is trimmed.
                                elide: Text.ElideLeft
                                text: channelRow.modelData.text.trim()
                                font: pitchColumn.font
                                color: channelRow.textColor
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onDoubleClicked: {
                                if (audioDecoder) {
                                    audioDecoder.decodeChannel(Math.round(channelRow.modelData.pitch))
                                }
                            }
                        }
                    }
                }
            }

            // Receive and Transcript text, split by a draggable
            // handle. Pane widths only change when the handle is dragged (or the
            // window resized), never in response to the text content.
            SplitView {
                id: textSplitView
                SplitView.fillHeight: true
                SplitView.minimumHeight: 60
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.pressed ? cwSenderWindow.palette.highlight
                         : SplitHandle.hovered ? cwSenderWindow.palette.mid
                         : cwSenderWindow.palette.window
                }
            
                // Receive text (only visible if enabled)
                ScrollView {
                    id: receiveScrollView
                    SplitView.preferredWidth: textSplitView.width / 2
                    SplitView.minimumWidth: 100
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    visible: cwSenderController ? cwSenderController.receiveVisible : false
                
                    TextArea {
                        id: receiveText
                        readOnly: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextArea.Wrap
                        font.family: "Courier"
                        font.pixelSize: 12
                        color: cwSenderWindow.palette.text
                        background: Rectangle { color: cwSenderWindow.palette.base }
                    }
                }
            
                // Transcript text
                ScrollView {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 100
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                
                    TextArea {
                        id: transcriptText
                        readOnly: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextArea.Wrap
                        font.family: "Courier"
                        font.pixelSize: 12
                        placeholderText: qsTr("CW Transmission Transcript")
                        color: cwSenderWindow.palette.text
                        background: Rectangle { color: cwSenderWindow.palette.base }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: audioDecoder ? audioDecoder.status : ""
            elide: Text.ElideRight
            color: cwSenderWindow.palette.windowText
        }
        
        // Text input and send controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            
            TextField {
                id: textToSendEdit
                Layout.fillWidth: true
                enabled: cwSenderController ? cwSenderController.canSendCW : false
                font.family: "Courier"
                font.pixelSize: 12
                placeholderText: qsTr("Text to send...")
                color: enabled ? cwSenderWindow.palette.text : cwSenderWindow.palette.disabled.text
                background: Rectangle {
                    color: textToSendEdit.enabled ? cwSenderWindow.palette.base : cwSenderWindow.palette.disabled.base
                    border.color: textToSendEdit.enabled ? cwSenderWindow.palette.mid : cwSenderWindow.palette.disabled.mid
                }
                
                onTextChanged: {
                    if (cwSenderController) {
                        cwSenderController.textChanged(text)
                    }
                }
                
                Keys.onReturnPressed: {
                    if (cwSenderController) {
                        cwSenderController.sendText(text)
                    }
                }
            }
            
            Button {
                text: qsTr("Send")
                font.bold: true
                implicitWidth: 80
                enabled: cwSenderController ? cwSenderController.canSendCW : false
                onClicked: {
                    if (cwSenderController) {
                        cwSenderController.sendText(textToSendEdit.text)
                    }
                }
            }
            
            Button {
                text: qsTr("Stop")
                font.bold: true
                implicitWidth: 80
                enabled: cwSenderController ? cwSenderController.canSendCW : false
                onClicked: {
                    if (cwSenderController) {
                        cwSenderController.stopSending()
                    }
                }
            }

            Button {
                text: qsTr("Auto Tune")
                visible: cwSenderController ? cwSenderController.cwAutoTuneAvailable : false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Ask the radio to tune onto the CW signal being received.")
                onClicked: {
                    if (cwSenderController) {
                        cwSenderController.cwAutoTune()
                    }
                }
            }
        }
        
        // Settings row
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            CheckBox {
                text: qsTr("Cut Numbers")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Send 0 as T and 9 as N. Applies to macro buttons only, not typed text.")
                checked: cwSenderController ? cwSenderController.cutNumbers : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.cutNumbers = checked
                    }
                }
            }
            
            CheckBox {
                text: qsTr("Send Immediate")
                checked: cwSenderController ? cwSenderController.sendImmediate : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.sendImmediate = checked
                    }
                }
            }
            
            CheckBox {
                text: qsTr("CW Decode")
                visible: cwSenderController ? cwSenderController.cwDecodeAvailable : false
                enabled: cwSenderController ? !cwSenderController.cwDecodeBusy : false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Turn the radio's CW decoder on or off. Decoded text appears in the left pane.")
                // Shows the radio's confirmed state; clicking asks the controller to change it.
                checked: cwSenderController ? cwSenderController.cwDecodeActive : false
                onToggled: {
                    if (cwSenderController) {
                        cwSenderController.setCwDecode(checked)
                        checked = Qt.binding(function() {
                            return cwSenderController ? cwSenderController.cwDecodeActive : false
                        })
                    }
                }
            }

            CheckBox {
                text: qsTr("Edit Macros")
                checked: cwSenderController ? cwSenderController.macroEditMode : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.macroEditMode = checked
                    }
                }
            }
            
            Label {
                text: qsTr("Sequence:")
            }
            
            SpinBox {
                from: 1
                to: 999
                editable: true
                inputMethodHints: Qt.ImhDigitsOnly
                value: cwSenderController ? cwSenderController.sequenceNumber : 1
                onValueChanged: {
                    if (cwSenderController) {
                        cwSenderController.sequenceNumber = value
                    }
                }
            }
        }
        
        // CW Parameters
        GridLayout {
            Layout.fillWidth: true
            columns: 6
            columnSpacing: 10
            rowSpacing: 5
            
            Label { text: qsTr("WPM:") }
            SpinBox {
                from: 6
                to: 48
                editable: true
                inputMethodHints: Qt.ImhDigitsOnly
                value: cwSenderController ? cwSenderController.wpm : 20
                onValueModified: {
                    if (cwSenderController) {
                        cwSenderController.wpm = value
                    }
                }
            }
            
            Label { text: qsTr("Dash Ratio:") }
            RowLayout {
                spacing: 10

                SpinBox {
                    from: 25
                    to: 45
                    stepSize: 1
                    editable: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    // The default validator only accepts integers, which would
                    // reject the "3.0" style text shown here.
                    validator: DoubleValidator {
                        bottom: 2.5
                        top: 4.5
                        decimals: 1
                        notation: DoubleValidator.StandardNotation
                    }
                    value: cwSenderController ? (cwSenderController.dashRatio * 10) : 30
                    textFromValue: function(value, locale) {
                        return Number(value / 10).toLocaleString(locale, "f", 1)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(Number.fromLocaleString(locale, text) * 10)
                    }
                    onValueModified: {
                        if (cwSenderController) {
                            cwSenderController.dashRatio = value / 10.0
                        }
                    }
                }

                Label {
                    text: qsTr("CW Decode Filter:")
                    visible: cwSenderController ? cwSenderController.cwDecodeFilterAvailable : false
                }
                ComboBox {
                    visible: cwSenderController ? cwSenderController.cwDecodeFilterAvailable : false
                    model: [qsTr("Off"), qsTr("Normal"), qsTr("Narrow")]
                    currentIndex: cwSenderController ? cwSenderController.cwDecodeFilter : 0
                    onActivated: function(index) {
                        if (cwSenderController) {
                            cwSenderController.cwDecodeFilter = index
                        }
                    }
                }
            }
            
            Label { text: qsTr("Pitch:") }
            SpinBox {
                // The range comes from the rig file's CW Pitch command.
                from: cwSenderController ? cwSenderController.pitchMin : 300
                to: cwSenderController ? cwSenderController.pitchMax : 900
                stepSize: 50
                editable: true
                inputMethodHints: Qt.ImhDigitsOnly
                value: cwSenderController ? cwSenderController.pitch : 600
                onValueModified: {
                    if (cwSenderController) {
                        cwSenderController.pitch = value
                    }
                }
            }
            
            Label { text: qsTr("Break-in:") }
            ComboBox {
                model: [qsTr("Off"), qsTr("Semi"), qsTr("Full")]
                currentIndex: cwSenderController ? cwSenderController.breakInMode : 0
                onCurrentIndexChanged: {
                    if (cwSenderController) {
                        cwSenderController.breakInMode = currentIndex
                    }
                }
            }
            
            CheckBox {
                readonly property bool available: cwSenderController ? cwSenderController.sidetoneAvailable : false
                // The sidetone is mixed into RX audio, so it needs a local RX audio path.
                text: available ? qsTr("Sidetone") : qsTr("Sidetone (no RX audio)")
                enabled: available
                checked: cwSenderController ? cwSenderController.sidetoneEnable : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.sidetoneEnable = checked
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                // Last item in the grid: take the empty cells to the right too.
                Layout.columnSpan: 3
                spacing: 10

                Slider {
                    WheelArea {}
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: cwSenderController ? cwSenderController.sidetoneLevel : 50
                    enabled: cwSenderController ? cwSenderController.sidetoneEnable && cwSenderController.sidetoneAvailable : false
                    onValueChanged: {
                        if (cwSenderController) {
                            cwSenderController.sidetoneLevel = value
                        }
                    }
                }

                Label {
                    text: qsTr("Decode Threshold:")
                    visible: cwSenderController ? cwSenderController.cwDecodeThresholdAvailable : false
                }
                Slider {
                    id: decodeThresholdSlider
                    WheelArea {}
                    Layout.fillWidth: true
                    visible: cwSenderController ? cwSenderController.cwDecodeThresholdAvailable : false
                    // The range comes from the rig file's CW Decode Threshold command.
                    from: cwSenderController ? cwSenderController.cwDecodeThresholdMin : 1
                    to: cwSenderController ? cwSenderController.cwDecodeThresholdMax : 30
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: cwSenderController ? cwSenderController.cwDecodeThreshold : 10
                    onMoved: {
                        if (cwSenderController) {
                            cwSenderController.cwDecodeThreshold = Math.round(value)
                        }
                    }
                }
                Label {
                    visible: decodeThresholdSlider.visible
                    text: Math.round(decodeThresholdSlider.value)
                    Layout.preferredWidth: 20
                }
            }
        }

        // Macro buttons (2 rows of 5)
        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 2
            rowSpacing: 2

            Repeater {
                id: macroRepeater
                model: 10

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    property int macroIndex: index + 1
                    text: cwSenderController ? (cwSenderController.getMacroButtonText(macroIndex) || ("M" + macroIndex)) : ("M" + macroIndex)
                    enabled: cwSenderController ? cwSenderController.canSendCW : false

                    onClicked: {
                        if (!cwSenderController) return

                        if (cwSenderController.macroEditMode) {
                            macroEditDialog.macroNumber = macroIndex
                            macroEditDialog.currentText = cwSenderController.getMacro(macroIndex)
                            macroEditDialog.open()
                        } else {
                            cwSenderController.runMacro(macroIndex)
                        }
                    }

                    onPressAndHold: {
                        if (!cwSenderController) return
                        macroEditDialog.macroNumber = macroIndex
                        macroEditDialog.currentText = cwSenderController.getMacro(macroIndex)
                        macroEditDialog.open()
                    }
                }
            }
        }
        
        // Status bar
        Label {
            id: statusBar
            Layout.fillWidth: true
            text: cwSenderController ? cwSenderController.statusMessage : ""
            color: text.indexOf(qsTr("Note:")) === 0 ? "#ffd166" : cwSenderWindow.palette.windowText
            padding: 5
            background: Rectangle {
                color: cwSenderWindow.palette.base
                border.color: cwSenderWindow.palette.mid
            }
        }
    }
    
    // Macro Edit Dialog
    Dialog {
        id: macroEditDialog
        title: qsTr("Edit Macro ") + macroNumber
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        property int macroNumber: 1
        property string currentText: ""
        
        x: Math.round((cwSenderWindow.width - width) / 2)
        y: Math.round((cwSenderWindow.height - height) / 2)

        background: Rectangle {
            color: cwSenderWindow.palette.window
            border.color: cwSenderWindow.palette.highlight
            border.width: 1
            radius: 4
        }
        
        ColumnLayout {
            spacing: 10
            
            Label {
                text: qsTr("Enter macro text (max 60 characters).\nUse %1 for sequence number.")
                wrapMode: Text.WordWrap
            }
            
            TextField {
                id: macroTextField
                Layout.preferredWidth: 400
                text: macroEditDialog.currentText
                maximumLength: 60
                placeholderText: qsTr("Macro text...")
            }
        }
        
        onOpened: {
            macroTextField.text = currentText
            macroTextField.forceActiveFocus()
        }
        
        onAccepted: {
            if (cwSenderController) {
                cwSenderController.editMacro(macroNumber, macroTextField.text)
            }
        }
    }
}
