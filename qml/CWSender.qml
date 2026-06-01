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
    height: 451
    color: palette.window

    palette {
        window: MainController.settings.options["Color.Window"]
        windowText: MainController.settings.options["Color.WindowText"]
        base: MainController.settings.options["Color.Base"]
        alternateBase: MainController.settings.options["Color.AlternateBase"]
        text: MainController.settings.options["Color.MainText"]
        button: MainController.settings.options["Color.Button"]
        buttonText: MainController.settings.options["Color.ButtonText"]
        brightText: MainController.settings.options["Color.BrightText"]
        highlight: MainController.settings.options["Color.Highlight"]
        highlightedText: MainController.settings.options["Color.HighlightedText"]
        mid: MainController.settings.options["Color.Mid"]
        dark: MainController.settings.options["Color.Dark"]
        light: MainController.settings.options["Color.Light"]
        placeholderText: MainController.settings.options["Color.PlaceholderText"]
    }

    property var cwSenderController: null
    
    visible: cwSenderController ? cwSenderController.visible : false

    function applyDisabledPalette() {
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
            receiveText.append(text)
        }
        
        function onTranscriptTextAppended(text) {
            transcriptText.append(text)
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
        target: MainController.settings
        function onColChanged(items) {
            cwSenderWindow.applyDisabledPalette()
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        
        // Top section: Receive and Transcript text
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 5
            
            // Receive text (only visible if enabled)
            ScrollView {
                id: receiveScrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: cwSenderController ? cwSenderController.receiveVisible : false
                
                TextArea {
                    id: receiveText
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    font.family: "Courier"
                    font.pixelSize: 12
                    color: cwSenderWindow.palette.text
                    background: Rectangle { color: cwSenderWindow.palette.base }
                }
            }
            
            // Transcript text
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                TextArea {
                    id: transcriptText
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    font.family: "Courier"
                    font.pixelSize: 12
                    placeholderText: qsTr("CW Transmission Transcript")
                    color: cwSenderWindow.palette.text
                    background: Rectangle { color: cwSenderWindow.palette.base }
                }
            }
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
        }
        
        // Settings row
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            CheckBox {
                text: qsTr("Cut Numbers")
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
                value: cwSenderController ? cwSenderController.wpm : 20
                onValueModified: {
                    if (cwSenderController) {
                        cwSenderController.wpm = value
                    }
                }
            }
            
            Label { text: qsTr("Dash Ratio:") }
            SpinBox {
                from: 25
                to: 45
                stepSize: 1
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
            
            Label { text: qsTr("Pitch:") }
            SpinBox {
                from: 300
                to: 900
                stepSize: 50
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
                text: qsTr("Sidetone")
                checked: cwSenderController ? cwSenderController.sidetoneEnable : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.sidetoneEnable = checked
                    }
                }
            }
            
            Slider {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: cwSenderController ? cwSenderController.sidetoneLevel : 50
                enabled: cwSenderController ? cwSenderController.sidetoneEnable : false
                onValueChanged: {
                    if (cwSenderController) {
                        cwSenderController.sidetoneLevel = value
                    }
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
