import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Window {
    id: cwSenderWindow
    title: "CW Sender"
    width: 835
    height: 451
    
    property var cwSenderController: null
    
    visible: cwSenderController ? cwSenderController.visible : false
    
    Component.onCompleted: {
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
                    color: "white"
                    background: Rectangle { color: "#2b2b2b" }
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
                    placeholderText: "CW Transmission Transcript"
                    color: "white"
                    background: Rectangle { color: "#2b2b2b" }
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
                font.family: "Courier"
                font.pixelSize: 12
                placeholderText: "Text to send..."
                color: "white"
                background: Rectangle {
                    color: "#2b2b2b"
                    border.color: "#555"
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
                text: "Send"
                font.bold: true
                implicitWidth: 80
                onClicked: {
                    if (cwSenderController) {
                        cwSenderController.sendText(textToSendEdit.text)
                    }
                }
            }
            
            Button {
                text: "Stop"
                font.bold: true
                implicitWidth: 80
                onClicked: {
                    if (cwSenderController) {
                        cwSenderController.stopSending()
                    }
                }
            }
        }
        
        // Macro buttons (2 rows of 5)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            
            Repeater {
                model: 2
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    
                    Repeater {
                        id: macroRepeater
                        model: 5
                        
                        Button {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            property int macroIndex: (parent.parent.model * 5) + index + 1
                            text: cwSenderController ? cwSenderController.getMacroButtonText(macroIndex) || ("M" + macroIndex) : ("M" + macroIndex)
                            
                            onClicked: {
                                if (!cwSenderController) return
                                
                                if (cwSenderController.macroEditMode) {
                                    // Show edit dialog
                                    macroEditDialog.macroNumber = macroIndex
                                    macroEditDialog.currentText = cwSenderController.getMacro(macroIndex)
                                    macroEditDialog.open()
                                } else {
                                    cwSenderController.runMacro(macroIndex)
                                }
                            }
                            
                            onPressAndHold: {
                                // Right-click or long press to edit
                                if (!cwSenderController) return
                                macroEditDialog.macroNumber = macroIndex
                                macroEditDialog.currentText = cwSenderController.getMacro(macroIndex)
                                macroEditDialog.open()
                            }
                        }
                    }
                }
            }
        }
        
        // Settings row
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            CheckBox {
                text: "Cut Numbers"
                checked: cwSenderController ? cwSenderController.cutNumbers : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.cutNumbers = checked
                    }
                }
            }
            
            CheckBox {
                text: "Send Immediate"
                checked: cwSenderController ? cwSenderController.sendImmediate : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.sendImmediate = checked
                    }
                }
            }
            
            CheckBox {
                text: "Edit Macros"
                checked: cwSenderController ? cwSenderController.macroEditMode : false
                onCheckedChanged: {
                    if (cwSenderController) {
                        cwSenderController.macroEditMode = checked
                    }
                }
            }
            
            Label {
                text: "Sequence:"
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
            
            Label { text: "WPM:" }
            SpinBox {
                from: 6
                to: 48
                value: cwSenderController ? cwSenderController.wpm : 20
                onValueChanged: {
                    if (cwSenderController) {
                        cwSenderController.wpm = value
                    }
                }
            }
            
            Label { text: "Dash Ratio:" }
            SpinBox {
                from: 25
                to: 45
                value: cwSenderController ? (cwSenderController.dashRatio * 10) : 30
                onValueChanged: {
                    if (cwSenderController) {
                        cwSenderController.dashRatio = value / 10.0
                    }
                }
            }
            
            Label { text: "Pitch:" }
            SpinBox {
                from: 300
                to: 900
                stepSize: 50
                value: cwSenderController ? cwSenderController.pitch : 600
                onValueChanged: {
                    if (cwSenderController) {
                        cwSenderController.pitch = value
                    }
                }
            }
            
            Label { text: "Break-in:" }
            ComboBox {
                model: ["Off", "Semi", "Full"]
                currentIndex: cwSenderController ? cwSenderController.breakInMode : 0
                onCurrentIndexChanged: {
                    if (cwSenderController) {
                        cwSenderController.breakInMode = currentIndex
                    }
                }
            }
            
            CheckBox {
                text: "Sidetone"
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
        
        // Status bar
        Label {
            id: statusBar
            Layout.fillWidth: true
            text: cwSenderController ? cwSenderController.statusMessage : ""
            padding: 5
            background: Rectangle {
                color: "#333"
                border.color: "#555"
            }
        }
    }
    
    // Macro Edit Dialog
    Dialog {
        id: macroEditDialog
        title: "Edit Macro " + macroNumber
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        property int macroNumber: 1
        property string currentText: ""
        
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        
        ColumnLayout {
            spacing: 10
            
            Label {
                text: "Enter macro text (max 60 characters).\nUse %1 for sequence number."
                wrapMode: Text.WordWrap
            }
            
            TextField {
                id: macroTextField
                Layout.preferredWidth: 400
                text: macroEditDialog.currentText
                maximumLength: 60
                placeholderText: "Macro text..."
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
