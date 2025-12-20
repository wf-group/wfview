// scope.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

Control {

    id: root
    anchors.fill: parent

    padding: 0

    palette.window: Theme.window
    palette.windowText: Theme.windowText
    palette.base: Theme.base
    palette.text: Theme.text
    palette.button: Theme.button
    palette.buttonText: Theme.buttonText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightedText

    property var rigCreatorWindow: null

    background: Rectangle {
        border.width: 0
        radius: 0
        color: root.palette.window
    }

    // MAIN CONTENT: SplitView above, freq bar below
    SplitView {
        id: splitView

        background: null
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: freqPane.top
        }
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 5
            width: parent.width
            color: "transparent"
            //color: Theme.window
        }

        SpectrumItem {
            id: spectrum
            objectName: "spectrum"
            //SplitView.minimumHeight: 60
            SplitView.preferredHeight: (splitView.height * 0.5) + 15
        }

        WaterfallItem {
            id: waterfall
            objectName: "waterfall"
            //SplitView.minimumHeight: 60
            SplitView.preferredHeight: (splitView.height * 0.5) - 15
            length: 128
        }
    }

    Item {
        id: freqPane
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        // enough height for both rows
        height: freqColumn.implicitHeight

        Rectangle {
            color: "transparent"
            anchors.fill: parent
            ColumnLayout {
                id: freqColumn
                anchors.fill: parent
                anchors.margins: 4
                spacing: 2

                // First row – FreqCtrlQuick items
                RowLayout {
                    id: freqRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36    // <— give it some height

                    // Left freq control
                    FreqCtrlQuick {
                        id: leftFreq
                        objectName: "leftFreq"
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true     // <— use layout height
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        frequency: 145500000
                        visible: false              // C++ can still toggle this
                    }

                    Button { id: vfoButton; text: "VFOA"; visible:false }

                    Item {
                        id: centerStrip
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Row {
                            id: midButtons
                            anchors.centerIn: parent
                            spacing: 6

                            Button { id: swapABButton;text: "A↔B"; visible:false }
                            Button { id: equalsABButton; text: "A=B"; visible:false }
                            Button { id: vmButton; text: "V/M"; visible:false }
                            Button { id: satButton; text: "SAT"; visible:false }
                            Button { id: splitButton; text: "SPLIT"; visible:false }
                            Button {
                                id: test;
                                text: "Test";
                                onClicked: {
                                    if (!rigCreatorWindow) {
                                        rigCreatorWindow =
                                            Qt.createComponent("RigCreator.qml").createObject(null)
                                    }
                                    rigCreatorWindow.visible = true
                                    rigCreatorWindow.raise()
                                    rigCreatorWindow.requestActivate()
                                }
                            }
                        }
                    }

                    // Right freq control
                    FreqCtrlQuick {
                        id: rightFreq
                        objectName: "rightFreq"
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true     // <— use layout height
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        frequency: 145500000
                        visible: false
                    }
                }

                // Second row – new controls
                RowLayout {
                    id: controlRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    spacing: 6

                    Button {
                        id: detachButton
                        objectName: "detach"
                        text: checked ? "Attach" : "Detach"
                        checkable: true
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.text: "Detach/re-attach scope from main window"
                        onToggled: receiver.detachScopeFromQml(checked)
                        Layout.preferredWidth: 50

                    }

                    ComboBox {
                        id: scopeModeCombo
                        objectName: "scopeMode"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.scopeModeValue
                        onActivated: receiver.scopeModeValue = currentValue;
                        Layout.preferredWidth: 100
                    }

                    ComboBox {
                        id: scopeSpanCombo
                        objectName: "scopeSpan"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.scopeSpanValue
                        visible: scopeModeCombo.currentValue === 0;
                        onActivated: receiver.scopeSpanValue = currentValue;
                        Layout.preferredWidth: 80
                    }

                    ComboBox {
                        id: scopeEdgeCombo
                        objectName: "scopeEdge"
                        textRole: "text"
                        valueRole: "value"
                        visible: scopeModeCombo.currentValue > 0;
                        currentValue: receiver.scopeEdgeValue
                        onActivated: receiver.scopeEdgeValue = currentValue;
                        Layout.preferredWidth: 110
                    }

                    Button {
                        id: customEdgeButton
                        objectName: "customEdge"
                        visible: scopeModeCombo.currentValue > 0;
                        text: "Custom Edge"
                        onClicked: customEdgeDialog.open()
                        Layout.preferredWidth: 90
                    }

                    Button {
                        id: toFixedButton
                        objectName: "toFixed"
                        text: "To Fixed"
                        visible: scopeModeCombo.currentValue === 0;
                        onClicked: edgeDialog.open()
                        Layout.preferredWidth: 70
                    }

                    Button {
                        id: holdButton
                        objectName: "hold"
                        text: "Hold"
                        checkable: true
                        checked: receiver.hold
                        onClicked: receiver.hold = checked
                        highlighted: checked
                        Layout.preferredWidth: 50
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    ComboBox {
                        id: modesCombo
                        objectName: "modes"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.modeValue
                        onActivated: receiver.modeValue = currentValue;
                        Layout.preferredWidth: 65
                    }

                    ComboBox {
                        id: dataModeCombo
                        objectName: "dataMode"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.dataModeValue
                        onActivated: receiver.dataModeValue = currentValue;
                        Layout.preferredWidth: 80
                    }

                    ComboBox {
                        id: filtersCombo
                        objectName: "filters"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.filterValue
                        onActivated: receiver.filterValue = currentValue;
                        Layout.preferredWidth: 65
                    }

                    ComboBox {
                        id: filterShapeCombo
                        objectName: "filterShape"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.filterShapeValue
                        onActivated: receiver.filterShapeValue = currentValue;
                        Layout.preferredWidth: 65
                    }

                    ComboBox {
                        id: roofingCombo
                        objectName: "roofing"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.roofingValue
                        onActivated: receiver.roofingValue = currentValue;
                        Layout.preferredWidth: 65
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        id: clearPeaksButton
                        objectName: "clearPeaks"
                        text: "Clear Peaks"
                        onClicked: spectrum.clearPeaks()
                        Layout.preferredWidth: 80

                    }
                }
            }
        }
    }

    // Small dialog for entering edge number (used by toFixed)
    Dialog {
        //background: Theme.window
        id: edgeDialog
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        x: (parent ? parent.width  - width  : 0) / 2
        y: (parent ? parent.height - height : 0) / 2

        contentItem: ColumnLayout {

            spacing: 8

            Label {
                text: "Edge to replace:"
            }

            ComboBox {
                id: edgeField
                model: [ "1", "2", "3", "4" ]
                Layout.fillWidth: true
            }
        }

        onAccepted: {
            var v = parseInt(edgeField.currentText, 10)
            if (!isNaN(v))
                receiver.toFixed = v
        }
    }

    // Small dialog for entering a custom edge
    Dialog {
        id: customEdgeDialog
        modal: true

        // constraints (filled from C++)
        property int minKHz: 0
        property int maxKHz: 0
        property int minSpan: 1
        property int maxSpan: 999999

        onOpened: {
            const p = receiver.customEdgeFreqDialogParams()

            // copy into dialog-scoped properties so isValid() can see them
            minKHz   = p.minKHz
            maxKHz   = p.maxKHz
            minSpan  = p.minSpan
            maxSpan  = p.maxSpan

            startEdgeFreq.from     = minKHz
            startEdgeFreq.to       = maxKHz
            startEdgeFreq.stepSize = minSpan
            startEdgeFreq.value    = p.startKHz

            endEdgeFreq.from       = minKHz
            endEdgeFreq.to         = maxKHz
            endEdgeFreq.stepSize   = minSpan
            endEdgeFreq.value      = p.endKHz

            // (re)bind OK enabled after values/constraints are set
            const okBtn = standardButton(Dialog.Ok)
            if (okBtn)
                okBtn.enabled = Qt.binding(() => customEdgeDialog.isValid())
        }

        function isValid() {
            return (endEdgeFreq.value - startEdgeFreq.value >= minSpan &&
                    endEdgeFreq.value - startEdgeFreq.value <= maxSpan)
        }

        standardButtons: Dialog.Ok | Dialog.Cancel

        x: (parent ? parent.width  - width  : 0) / 2
        y: (parent ? parent.height - height : 0) / 2

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: "Start frequency (kHz):"
            }

            SpinBox {
                id: startEdgeFreq
                editable: true
                Layout.fillWidth: true
            }

            Label {
                text: "End frequency (kHz):"
            }

            SpinBox {
                id: endEdgeFreq
                editable: true
                Layout.fillWidth: true
            }

            Label {
                text: "Invalid span size"
                color: "red"
                visible: !customEdgeDialog.isValid()
            }
        }
        Component.onCompleted: {
            const okBtn = standardButton(Dialog.Ok)
            okBtn.enabled = Qt.binding(() => customEdgeDialog.isValid())
        }
        onAccepted: {
            // safe: this will only fire when valid
            receiver.setCustomEdgeFreqs(startEdgeFreq.value,endEdgeFreq.value)
        }

    }


    // Drawer stays as-is, still anchored to root
    Drawer {
        id: sideDrawer
        edge: Qt.RightEdge
        height: parent.height

        palette.window: Theme.window
        palette.windowText: Theme.windowText
        palette.base: Theme.base
        palette.text: Theme.text
        palette.button: Theme.button
        palette.buttonText: Theme.buttonText
        palette.highlight: Theme.highlight
        palette.highlightedText: Theme.highlightedText
        background: Rectangle {
            color: Theme.window
            border.color: Theme.highlight
            border.width: 1
        }

        implicitWidth: Math.min(grid.implicitWidth * settingsContainer.scaleFactor + 24,
                                parent.width * 0.9)
        width: implicitWidth


        // --- Header bar ---
        Rectangle {
            Layout.fillWidth: true
            height: 30
            color: Theme.window
            ToolButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 6

                icon.name: "window-close"
                onClicked: sideDrawer.close()
            }
        }

        Flickable {
            id: flick
            anchors.fill: parent
            clip: true
            contentWidth: settingsContainer.width
            contentHeight: sideDrawer.height

            Item {
                id: settingsContainer

                property real scaleFactor: {
                    if (grid.implicitHeight <= 0 || sideDrawer.height <= 0)
                        return 1.0;
                    return Math.min(1.0, sideDrawer.height / grid.implicitHeight);
                }

                GridLayout {
                    id: grid
                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 8

                    // Title row
                    Label {
                        text: "Scope settings"
                        font.bold: true
                        Layout.columnSpan: 2
                    }

                    // === Ref slider ===
                    Label {
                        text: "Ref"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: refSlider
                        objectName: "ref"
                        Layout.preferredWidth: 120
                        value: receiver.refValue
                        onMoved: receiver.refValue = value
                    }

                    // === Length slider ===
                    Label {
                        text: "Length"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: lengthSlider
                        from: 100
                        to: 1024
                        stepSize: 16
                        value: waterfall.length
                        Layout.preferredWidth: 120
                        onValueChanged: waterfall.length = value
                    }

                    // === Ceiling slider ===
                    Label {
                        text: "Ceiling"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: ceilingSlider
                        from: 0
                        to: 255
                        value: waterfall.ceiling
                        Layout.preferredWidth: 120
                        onValueChanged:  {
                            waterfall.ceiling = value;
                            spectrum.ceiling = value;
                        }
                    }

                    // === Floor slider ===
                    Label {
                        text: "Floor"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: floorSlider
                        from: 0
                        to: 255
                        value: waterfall.floor
                        Layout.preferredWidth: 120
                        onValueChanged:  {
                            waterfall.floor = value;
                            spectrum.floor = value;
                        }
                    }

                    // === Speed combobox ===
                    Label {
                        text: "Speed"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: speedCombo
                        objectName: "speed"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.speedValue
                        onActivated: receiver.speedValue = currentValue;
                        Layout.preferredWidth: 120
                    }

                    // === Theme combobox ===
                    Label {
                        text: "Theme"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: themeCombo                        
                        objectName: "theme"
                        textRole: "text"
                        valueRole: "value"
                        currentValue: receiver.themeValue
                        onActivated: receiver.themeValue = currentValue;
                        Layout.preferredWidth: 120
                    }

                    // === PBT Inner slider ===
                    Label {
                        text: "PBT Inner"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: pbtInnerSlider
                        objectName: "pbtInner"
                        value: receiver.pbtInnerValue
                        onMoved: receiver.pbtInnerValue = value
                        Layout.preferredWidth: 120
                        // onValueChanged: backend.pbtInner = value
                    }

                    // === PBT Outer slider ===
                    Label {
                        text: "PBT Outer"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: pbtOuterSlider
                        objectName: "pbtOuter"
                        value: receiver.pbtOuterValue
                        onMoved: receiver.pbtOuterValue = value
                        Layout.preferredWidth: 120
                        // onValueChanged: backend.pbtOuter = value
                    }

                    // === IF Shift slider + reset button ===
                    Label {
                        text: "IF Shift"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    RowLayout {
                        Layout.preferredWidth: 120
                        spacing: 6

                        Slider {
                            id: ifShiftSlider
                            objectName: "ifShift"
                            value: receiver.ifShiftValue
                            onMoved: receiver.ifShiftValue = value
                            Layout.fillWidth: true
                            // onValueChanged: backend.ifShift = value
                        }

                        Button {
                            text: "R"
                            Layout.preferredWidth: 32
                            onClicked: ifShiftSlider.value = 0
                        }
                    }

                    // === Filter width slider ===
                    Label {
                        text: "Filter width"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: filterWidthSlider
                        objectName: "filterWidth"
                        value: receiver.filterWidthValue
                        onMoved: receiver.filterWidthValue = value
                        Layout.preferredWidth: 120
                        // onValueChanged: backend.filterWidth = value
                    }

                    // === Scope enabled checkbox ===
                    Label {
                        text: "Scope enabled"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    CheckBox {
                        id: scopeEnabledCheckBox
                        text: ""
                        checked: true
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        onCheckedChanged: {
                            splitView.visible = checked
                            splitView.enabled = checked
                        }
                    }
                }
            }
        }
    }

    ToolButton {
        id: drawerButton
        text: "\u2630"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        onClicked: sideDrawer.open()
    }
}

