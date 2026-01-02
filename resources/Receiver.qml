// scope.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

pragma ComponentBehavior: Bound

Control {

    id: root

    readonly property var hostWin: root.Window.window

    //Layout.fillWidth: true
    //Layout.fillHeight: true

    property int receiverIndex: 0
    property var controller: null
    signal requestDetach(var globalPos)
    padding: 1
    clip: false

    palette.window: Theme.window
    palette.windowText: Theme.windowText
    palette.base: Theme.base
    palette.text: Theme.text
    palette.button: Theme.button
    palette.buttonText: Theme.buttonText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightedText

    background: Rectangle {
        color: root.palette.window
        radius: 0
        border.width: 1
        border.color: root.controller.active ? "red"
                                  : Qt.darker(root.palette.window, 1.4)
        antialiasing: true
    }

    contentItem: ColumnLayout {
        id: contentLayout
        spacing: 4

        Label {
            id: header
            text: root.controller && root.controller.title
            font.bold: true
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            padding: 4
            background: Rectangle {
                color: Qt.darker(root.palette.window, 1.05)
                radius: 2
            }
            MouseArea {
                id: grab
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor

                property bool dragging: false
                property point pressGlobal: Qt.point(0,0)

                onPressed: function(mouse) {
                    Logging.info("Clicked!")
                    dragging = true
                    pressGlobal = header.mapToGlobal(mouse.x, mouse.y)
                }

                onPositionChanged: function(mouse) {
                    if (!dragging) return

                    const p = header.mapToItem(win.contentItem, mouse.x, mouse.y)
                    Logging.info("Dragging!")

                    // Detach trigger: pointer leaves the main window by a margin
                    // We'll let MainWindow decide, but we can do a simple threshold here too.
                    // Emit continuously or only when definitely outside:
                    root.requestDetach(p)
                }

                onReleased: dragging = false
                onCanceled: dragging = false
            }
        }

        SplitView {
            id: splitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            //background: null

            orientation: Qt.Vertical

            handle: Rectangle {
                implicitHeight: 5
                width: parent.width
                color: "transparent"
                //color: Theme.window
            }

            SpectrumItem {
                id: spectrum
                controller: root.controller
                SplitView.fillWidth: true
                SplitView.preferredHeight: (splitView.height * 0.5) + 15
                passbandLow: controller ? controller.passbandLow : 0
                passbandHigh: controller ? controller.passbandHigh : 0
                colors: controller ? controller.colors : null
                center: controller ? controller.frequencyA : null
                bands: controller && controller.activeBands
                onTuneRequested: function(freq) {
                    controller.setFrequencyA(freq*1000000.0,true)   // or controller.setFrequencyHz(freq)
                    controller.frequencyAChanged() // Signal the controller that frequency has changed
                }
            }

            WaterfallItem {
                id: waterfall
                controller: root.controller
                SplitView.fillWidth: true
                SplitView.preferredHeight: (splitView.height * 0.5) - 15
                theme: themeCombo.currentValue + 0 // Hack to stop linter complaining!
                length: lengthSlider.value
                onTuneRequested: function(freq) {
                    controller.setFrequencyA(freq*1000000.0,true)   // or controller.setFrequencyHz(freq)
                    controller.frequencyAChanged() // Signal the controller that frequency has changed
                }
            }
        }

        Item {
            id: freqPane
            Layout.fillWidth: true
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
                            // push controller config into the control
                            freqDigits: controller && controller.freqDisplay.digits
                            minFrequency: controller && controller.freqDisplay.min
                            maxFrequency: controller && controller.freqDisplay.max
                            freqMinStep: controller && controller.freqDisplay.minStep
                            unit: controller && controller.freqDisplay.unit
                            gsep: controller && controller.freqDisplay.gsep
                            dsep: controller && controller.freqDisplay.dsep
                            // pull current value from controller
                            frequency: controller && controller.frequencyA

                            // UI -> controller
                            //onNewFrequency: controller.onNewFrequencyFromUi(freq, 0)
                            visible: controller && (controller.uiFlags & ReceiverController.ShowVFOA) !== 0
                            onNewFrequency: function(freq) {
                                controller.setFrequencyA(freq,true)   // or controller.setFrequencyHz(freq)
                                controller.frequencyAChanged() // Signal the controller that frequency has changed
                            }

                        }

                        Button {
                            id: vfoButton;
                            text: "VFOA";
                            visible: controller && (controller.uiFlags & ReceiverController.ShowVfoButton) !== 0
                        }

                        Item {
                            id: centerStrip
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Row {
                                id: midButtons
                                anchors.centerIn: parent
                                spacing: 6

                                Button {
                                    id: swapABButton;
                                    text: "A↔B";
                                    visible: controller && (controller.uiFlags & ReceiverController.ShowSwapABButton) !== 0
                                }

                                Button {
                                    id: equalsABButton;
                                    text: "A=B";
                                    visible: controller && (controller.uiFlags & ReceiverController.ShowEqualsABButton) !== 0
                                }

                                Button {
                                    id: vmButton;
                                    text: "V/M";
                                    visible: controller && (controller.uiFlags & ReceiverController.ShowVMButton) !== 0
                                }

                                Button {
                                    id: satButton;
                                    text: "SAT";
                                    visible: controller && (controller.uiFlags & ReceiverController.ShowSatButton) !== 0
                                }

                                Button {
                                    id: splitButton;
                                    text: "SPLIT";
                                    visible: controller && (controller.uiFlags & ReceiverController.ShowSplitButton) !== 0
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
                            freqDigits: controller && controller.freqDigits
                            minFrequency: controller && controller.freqMin
                            maxFrequency: controller && controller.freqMax
                            freqMinStep: controller && controller.freqMinStep
                            unit: controller && controller.freqUnit
                            gsep: controller && controller.gsep
                            dsep: controller && controller.dsep
                            frequency: controller && controller.frequencyB
                            visible: controller && ((controller.uiFlags & ReceiverController.ShowVFOB) !== 0)
                            onNewFrequency: function(freq) {
                                controller.setFrequencyB(freq,true)   // or controller.setFrequencyHz(freq)
                                controller.frequencyBChanged() // Signal the controller that frequency has changed
                            }
                        }
                    }

                    // Second row – new controls
                    RowLayout {
                        id: controlRow
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        spacing: 6

                        ComboBox {
                            id: scopeModeCombo
                            readonly property var spec: controller ? controller.uiSpecs["scopeModes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.scopeModeValue : undefined
                            onActivated: controller.scopeModeValue = currentValue
                            Layout.preferredWidth: 100
                        }

                        ComboBox {
                            id: scopeSpanCombo
                            readonly property var spec: controller ? controller.uiSpecs["scopeSpans"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.scopeSpanValue : undefined
                            onActivated: controller.scopeSpanValue = currentValue
                            Layout.preferredWidth: 80
                        }

                        ComboBox {
                            id: scopeEdgeCombo
                            readonly property var spec: controller ? controller.uiSpecs["scopeEdges"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.scopeEdgeValue : undefined
                            onActivated: controller.scopeEdgeValue = currentValue
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
                            checked: controller && controller.hold
                            onClicked: controller.hold = checked
                            highlighted: checked
                            Layout.preferredWidth: 50
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ComboBox {
                            id: modesCombo
                            readonly property var spec: controller ? controller.uiSpecs["modes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.modeValue : undefined
                            onActivated: controller.modeValue = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: dataModeCombo
                            readonly property var spec: controller ? controller.uiSpecs["dataModes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.dataModeValue : undefined
                            onActivated: controller.dataModeValue = currentValue
                            Layout.preferredWidth: 80
                        }

                        ComboBox {
                            id: filtersCombo
                            readonly property var spec: controller ? controller.uiSpecs["filters"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.filterValue : undefined
                            onActivated: controller.filterValue = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: filterShapeCombo
                            readonly property var spec: controller ? controller.uiSpecs["filterShapes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.filterShapeValue : undefined
                            onActivated: controller.filterShapeValue = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: roofingCombo
                            readonly property var spec: controller ? controller.uiSpecs["roofingFilters"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.roofingValue : undefined
                            onActivated: controller.roofingValue = currentValue
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
    }

    // Drawer stays as-is, still anchored to root
    Drawer {
        id: sideDrawer

        parent: root.hostWin ? root.hostWin.contentItem : null

        edge: Qt.RightEdge

        // Use the host window dimensions, not `parent`
        height: root.hostWin ? root.hostWin.height : 0
        implicitWidth: Math.min(grid.implicitWidth * settingsContainer.scaleFactor + 24,
                                (root.hostWin ? root.hostWin.width : 0) * 0.9)
        width: implicitWidth

        // When it moves between windows, force it closed (prevents "stuck on old overlay")
        onParentChanged: close()

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
                        text: controller && controller.drawerTitle
                        font.bold: true
                        Layout.columnSpan: 2
                        Layout.alignment: Qt.AlignRight
                    }

                    // === Ref slider ===
                    Label {
                        text: "Ref"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        visible: refSlider.spec ? refSlider.spec.visible : false
                    }
                    Slider {
                        id: refSlider
                        readonly property var spec: controller ? controller.uiSpecs["refSlider"] : null
                        objectName: "ref"
                        Layout.preferredWidth: 120
                        from: spec ? spec.from : 0
                        to: spec ? spec.to : 0
                        value: controller && controller.refValue
                        onMoved: controller.refValue = value
                        visible: spec ? spec.visible : false

                        HoverHandler {
                            id: hover
                        }

                        ToolTip.visible: hover.hovered
                        ToolTip.text: Math.round(refSlider.value).toString()
                        ToolTip.delay: 300
                    }

                    // === Length slider ===
                    Label {
                        text: "Length"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: lengthSlider
                        from: 10
                        to: 256
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
                        readonly property var spec: controller ? controller.uiSpecs["speeds"] : null
                        model: spec ? spec.model : []
                        textRole: spec ? spec.textRole : "text"
                        valueRole: spec ? spec.valueRole : "value"
                        visible: spec ? (spec.visible ?? true) : false
                        currentValue: controller ? controller.speedValue : undefined
                        onActivated: controller.speedValue = currentValue
                        Layout.preferredWidth: 120
                    }

                    // === Theme combobox ===
                    Label {
                        text: "Theme"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: themeCombo
                        readonly property var spec: controller ? controller.uiSpecs["themes"] : null
                        model: spec ? spec.model : []
                        textRole: spec ? spec.textRole : "text"
                        valueRole: spec ? spec.valueRole : "value"
                        visible: spec ? (spec.visible ?? true) : false
                        currentValue: controller ? controller.themeValue : undefined
                        onActivated: controller.themeValue = currentValue
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
                        value: controller && controller.pbtInnerValue
                        onMoved: controller.pbtInnerValue = value
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
                        value: controller.pbtOuterValue
                        onMoved: controller.pbtOuterValue = value
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
                            value: controller && controller.ifShiftValue
                            onMoved: controller.ifShiftValue = value
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
                        value: controller && controller.filterWidthValue
                        onMoved: controller.filterWidthValue = value
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
                controller.toFixed = v
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
            const p = controller.customEdgeFreqDialogParams()

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
            controller.setCustomEdgeFreqs(startEdgeFreq.value,endEdgeFreq.value)
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

