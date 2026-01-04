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
        spacing: 2
        anchors.fill: parent

        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 28

            Meter {
                id: smeter
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.top: parent.top
                anchors.topMargin: 2
                width: 300
                height: 26
                meterType: root.controller ? root.controller.meterType : 0
                // only set the "current" level; MeterItem does peak/avg itself
                current: root.controller ? root.controller.meter : 0

                drawLabels: true
                Component.onCompleted: smeter.setMeterExtremities(-54, 60, 0)
            }

            Popup {
                id: meterTypePopup
                modal: false
                x: 0
                y: 0

                contentItem: ComboBox {
                    id: combo
                    model: [
                        { text:"None", value: 0 },          // meterNone
                        { text:"S-Meter", value: 1 },   // adjust enum values to match meter_t
                        { text:"Sub S-Meter", value: 2 },   // adjust enum values to match meter_t
                        { text:"SWR", value: 5 },
                        { text:"ALC", value: 4 },
                        { text:"Compression", value: 8 },
                        { text:"Voltage", value: 6 },
                        { text:"Current", value: 7 },
                        { text:"Center", value: 9 },
                        { text:"TxRxAudio", value: 10 },
                        { text:"RxAudio", value: 11 },
                        { text:"TxAudio", value: 12 }
                    ]
                    textRole: "text"
                    valueRole: "value"

                    onActivated: {
                        meter.meterType = currentValue
                        typePopup.close()
                    }
                }
            }

            // Full-width centering lane (NO background here)
            Item {
                id: titleLane
                anchors.fill: parent

                // Background pill sized to the text, centered absolutely
                Rectangle {
                    id: titleBg
                    anchors.centerIn: parent
                    radius: 2
                    color: Qt.darker(root.palette.window, 1.05)

                    // keep it out of the meter area when things get tight
                    readonly property int leftLimit: smeter.width + 8
                    readonly property int maxW: Math.max(0, header.width - leftLimit - 8)

                    width: Math.min(titleText.implicitWidth + 12, maxW)
                    height: Math.max(titleText.implicitHeight + 6, 18)

                    // if centering would overlap meter, clamp rightwards
                    x: Math.max((header.width - width) / 2, leftLimit)
                }

                Text {
                    id: titleText
                    anchors.centerIn: titleBg
                    text: root.controller && root.controller.title ? root.controller.title : ""
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    width: titleBg.width - 12
                }

                MouseArea {
                    id: grab
                    anchors.fill: titleBg
                    cursorShape: Qt.SizeAllCursor

                    property bool dragging: false

                    onPressed: function(mouse) {
                        dragging = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!dragging) return
                        const p = header.mapToItem(win.contentItem, mouse.x, mouse.y)
                        root.requestDetach(p)
                    }
                    onReleased: dragging = false
                    onCanceled: dragging = false

                    // your dragging logic here if you want it on the title pill
                }
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

                WheelHandler
                {
                    id: whSpectrum
                    //target: receiverRoot
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        if (!controller) return;
                        controller.onWheelTune(event.angleDelta.y, event.modifiers);
                        event.accepted = true;
                    }
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
                WheelHandler
                {
                    id: whWaterfall
                    //target: receiverRoot
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        if (!controller) return;
                        controller.onWheelTune(event.angleDelta.y, event.modifiers);
                        event.accepted = true;
                    }
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
                            currentValue: controller ? controller.scopeMode : undefined
                            onActivated: controller.scopeMode = currentValue
                            Layout.preferredWidth: 100
                        }

                        ComboBox {
                            id: scopeSpanCombo
                            readonly property var spec: controller ? controller.uiSpecs["scopeSpans"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.scopeSpan : undefined
                            onActivated: controller.scopeSpan = currentValue
                            Layout.preferredWidth: 80
                        }

                        ComboBox {
                            id: scopeEdgeCombo
                            readonly property var spec: controller ? controller.uiSpecs["scopeEdges"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.scopeEdge : undefined
                            onActivated: controller.scopeEdge = currentValue
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
                            currentValue: controller ? controller.mode : undefined
                            onActivated: controller.mode = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: dataModeCombo
                            readonly property var spec: controller ? controller.uiSpecs["dataModes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.dataMode : undefined
                            onActivated: controller.dataMode = currentValue
                            Layout.preferredWidth: 80
                        }

                        ComboBox {
                            id: filtersCombo
                            readonly property var spec: controller ? controller.uiSpecs["filters"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.filter : undefined
                            onActivated: controller.filter = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: filterShapeCombo
                            readonly property var spec: controller ? controller.uiSpecs["filterShapes"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.filterShape : undefined
                            onActivated: controller.filterShape = currentValue
                            Layout.preferredWidth: 65
                        }

                        ComboBox {
                            id: roofingCombo
                            readonly property var spec: controller ? controller.uiSpecs["roofingFilters"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: spec ? (spec.visible ?? true) : false
                            currentValue: controller ? controller.roofing : undefined
                            onActivated: controller.roofing = currentValue
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

        // Popup must live in the overlay to render reliably
        parent: root.hostWin ? root.hostWin.overlay : null
        edge: Qt.RightEdge

        width: Math.min(240, (root.hostWin ? root.hostWin.width : 240) * 0.9)
        height: root.hostWin ? root.hostWin.height : 0

        onParentChanged: close()

        background: Rectangle {
            color: Theme.window
            border.color: Theme.highlight
            border.width: 1
        }

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 30
                color: Theme.window

                ToolButton {
                    id: closeBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 6
                    icon.name: "window-close"
                    onClicked: sideDrawer.close()
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: closeBtn.left
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter

                    text: controller ? controller.drawerTitle : ""
                    font.bold: true
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Flickable {
                id: flick
                width: parent.width
                height: parent.height - 30
                clip: true

                contentWidth: width
                contentHeight: grid.implicitHeight + 24

                // vertical scrollbar
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                Item {
                    width: flick.width
                    height: flick.contentHeight

                    GridLayout {
                        id: grid
                        x: 12
                        y: 12
                        width: parent.width - 24
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 8


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
                            //Layout.preferredWidth: 120
                            from: spec ? spec.from : 0
                            Layout.fillWidth: true
                            to: spec ? spec.to : 0
                            value: controller && controller.ref
                            onMoved: controller.ref = value
                            visible: spec ? spec.visible : false
                            implicitHeight: 25

                            HoverHandler {
                                id: hover
                            }

                            ToolTip.visible: hover.hovered
                            ToolTip.text: Math.round(value/10.0).toString()+ "dB" // Divide by 10 to get dB
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
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            onValueChanged: waterfall.length = value
                            implicitHeight: 25

                            HoverHandler {
                                id: hoverLength
                            }

                            ToolTip.visible: hoverLength.hovered
                            ToolTip.text: Math.round(value).toString()
                            ToolTip.delay: 300
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
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            onValueChanged:  {
                                waterfall.ceiling = value;
                                spectrum.ceiling = value;
                            }
                            HoverHandler {
                                id: hoverCeiling
                            }

                            ToolTip.visible: hoverCeiling.hovered
                            ToolTip.text: Math.round(value).toString()
                            ToolTip.delay: 300
                            implicitHeight: 25
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
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            onValueChanged:  {
                                waterfall.floor = value;
                                spectrum.floor = value;
                            }
                            HoverHandler {
                                id: hoverFloor
                            }

                            ToolTip.visible: hoverFloor.hovered
                            ToolTip.text: Math.round(value).toString()
                            ToolTip.delay: 300
                            implicitHeight: 25
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
                            currentValue: controller ? controller.speed : undefined
                            onActivated: controller.speed = currentValue
                            Layout.fillWidth: true
                            //Layout.preferredWidth: 120
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
                            currentValue: controller ? controller.theme : undefined
                            onActivated: controller.theme = currentValue
                            Layout.fillWidth: true
                            //Layout.preferredWidth: 120
                        }

                        // === PBT Inner slider ===
                        Label {
                            text: "PBT Inner"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: pbtInnerSlider.visible
                        }
                        Slider {
                            id: pbtInnerSlider
                            readonly property var spec: controller ? controller.uiSpecs["pbtInnerSlider"] : null
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            value: controller && controller.pbtInner
                            onMoved: controller.pbtInner = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverPbtInner
                            }

                            ToolTip.visible: hoverPbtInner.hovered
                            ToolTip.text: Math.round(value).toString()
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }

                        // === PBT Outer slider ===
                        Label {
                            text: "PBT Outer"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: pbtOuterSlider.visible
                        }
                        Slider {
                            id: pbtOuterSlider
                            readonly property var spec: controller ? controller.uiSpecs["pbtOuterSlider"] : null
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            value: controller && controller.pbtInner
                            onMoved: controller.pbtInner = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverPbtOuter
                            }

                            ToolTip.visible: hoverPbtOuter.hovered
                            ToolTip.text: Math.round(value).toString()
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }

                        // === IF Shift slider + reset button ===
                        Label {
                            text: "IF Shift"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        RowLayout {
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            spacing: 2

                            Slider {
                                id: ifShiftSlider
                                objectName: "ifShift"
                                value: controller && controller.ifShift
                                onMoved: controller.ifShift = value
                                Layout.fillWidth: true
                                // onValueChanged: backend.ifShift = value
                            }

                            Button {
                                text: "R"
                                Layout.preferredWidth: 20
                                onClicked: ifShiftSlider.value = 0
                            }
                            implicitHeight: 25
                        }

                        // === Filter width slider ===
                        Label {
                            text: "Filter width"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        Slider {
                            id: filterWidthSlider
                            objectName: "filterWidth"
                            value: controller && controller.filterWidth
                            onMoved: controller.filterWidth = value
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            // onValueChanged: backend.filterWidth = value
                        }

                        // === NB checkbox + slider ===
                        RowLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            spacing: 0

                            Label {
                                text: "NB"
                            }

                            CheckBox {
                                id: nbCheckBox
                                checked: controller ? controller.nb : false
                                onCheckedChanged: {
                                    if (controller)
                                        controller.nb = checked
                                }
                            }
                        }

                        Slider {
                            id: nbSlider
                            readonly property var spec: controller ? controller.uiSpecs["nbSlider"] : null
                            value: controller ? controller.nbLevel : 0
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            onMoved: controller.nbLevel = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverNb
                            }

                            ToolTip.visible: hoverNb.hovered
                            ToolTip.text: Math.round((value - from) / (to - from) * 100).toString() + " %"
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }


                        RowLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            spacing: 0

                            Label {
                                text: "NR"
                            }

                            CheckBox {
                                id: nrCheckBox
                                checked: controller ? controller.nr : false
                                onCheckedChanged: {
                                    if (controller)
                                        controller.nr = checked
                                }
                            }
                        }

                        Slider {
                            id: nrSlider
                            readonly property var spec: controller ? controller.uiSpecs["nrSlider"] : null
                            value: controller ? controller.nrLevel : 0
                            //Layout.preferredWidth: 120
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            onMoved: controller.nrLevel = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverNr
                            }

                            ToolTip.visible: hoverNr.hovered
                            ToolTip.text: Math.round((value - from) / (to - from) * 100).toString() + " %"
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }

                        Label {
                            text: "RF Gain"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: rfGainSlider.visible
                        }
                        Slider {
                            id: rfGainSlider
                            readonly property var spec: controller ? controller.uiSpecs["rfGainSlider"] : null
                            value: controller ? controller.rfGain : 0
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            onMoved: controller.rfGain = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverRfGain
                            }

                            ToolTip.visible: hoverRfGain.hovered
                            ToolTip.text: Math.round((value - from) / (to - from) * 100).toString() + " %"
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }

                        Label {
                            text: "AF Gain"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: afGainSlider.visible
                        }
                        Slider {
                            id: afGainSlider
                            readonly property var spec: controller ? controller.uiSpecs["afGainSlider"] : null
                            value: controller ? controller.afGain : 0
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            onMoved: controller.nb = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverAfGain
                            }

                            ToolTip.visible: hoverAfGain.hovered
                            ToolTip.text: Math.round((value - from) / (to - from) * 100).toString() + " %"
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }

                        Label {
                            text: "Squelch"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: squelchSlider.visible
                        }
                        Slider {
                            id: squelchSlider
                            readonly property var spec: controller ? controller.uiSpecs["squelchSlider"] : null
                            value: controller ? controller.squelch : 0
                            Layout.fillWidth: true
                            from: spec ? spec.from : 0
                            to: spec ? spec.to : 0
                            onMoved: controller.squelch = value
                            visible: spec ? spec.visible : false

                            HoverHandler {
                                id: hoverSquelch
                            }

                            ToolTip.visible: hoverSquelch.hovered
                            ToolTip.text: Math.round((value - from) / (to - from) * 100).toString() + " %"
                            ToolTip.delay: 300
                            implicitHeight: 25
                        }


                        Label {
                            text: "Attenuator"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: attCtl.visible
                        }

                        Item {
                            id: attCtl
                            Layout.fillWidth: true
                            implicitHeight: 40

                            readonly property var spec: controller ? controller.uiSpecs["attenuator"] : null
                            visible: spec ? (spec.visible ?? true) : false

                            readonly property real sFrom: spec ? Number(spec.from ?? 0) : 0
                            readonly property real sTo:   spec ? Number(spec.to   ?? 45) : 45
                            readonly property real sStep: spec ? Math.max(1, Number(spec.step ?? 3)) : 3
                            readonly property string unit: spec ? String(spec.unit ?? "dB") : "dB"

                            readonly property int steps: {
                                if (sTo === sFrom) return 1;
                                return Math.floor(Math.abs((sTo - sFrom) / sStep)) + 1; // inclusive
                            }

                            // --- Tick/label styling (can be spec-driven later if you want) ---
                            property int endTickH: 8
                            property int midTickH: 4
                            property int tickPadL: 2
                            property int tickPadR: 2
                            property int labelY: 10

                            // Auto label density (ONLY labels, not ticks)
                            // Optional override: spec.labelEvery > 0 forces it
                            readonly property int labelEvery: spec ? Math.max(0, Number(spec.labelEvery ?? 0)) : 0
                            // Minimum pixel spacing between *labels* (auto mode)
                            readonly property int minLabelSpacingPx: spec ? Math.max(4, Number(spec.minLabelSpacingPx ?? 26)) : 26

                            readonly property int effectiveLabelEvery: {
                                if (labelEvery > 0) return labelEvery;
                                if (steps <= 1 || tickCanvas.width <= 0) return 1;

                                const drawW = Math.max(1, tickCanvas.width - tickPadL - tickPadR);
                                const pxPerStep = drawW / (steps - 1);
                                return Math.max(1, Math.ceil(minLabelSpacingPx / Math.max(1, pxPerStep)));
                            }

                            property bool syncing: false

                            Column {
                                anchors.fill: parent
                                spacing: 2

                                Slider {
                                    id: attSlider
                                    width: parent.width

                                    from: 0
                                    to: Math.max(0, attCtl.steps - 1)
                                    stepSize: 1
                                    snapMode: Slider.SnapAlways

                                    onMoved: {
                                        if (!controller || attCtl.syncing) return;
                                        controller.attenuator = attCtl.sFrom + Math.round(value) * attCtl.sStep;
                                    }

                                    HoverHandler { id: h }
                                    ToolTip.visible: h.hovered
                                    ToolTip.delay: 200
                                    ToolTip.text: (attCtl.sFrom + Math.round(attSlider.value) * attCtl.sStep) + attCtl.unit
                                }

                                Canvas {
                                    id: tickCanvas
                                    width: attSlider.width
                                    height: 20

                                    onPaint: {
                                        const ctx = getContext("2d");
                                        ctx.clearRect(0, 0, width, height);

                                        const n = attCtl.steps;
                                        if (n <= 1 || width <= 2) return;

                                        const denom = n - 1;

                                        const padL = attCtl.tickPadL;
                                        const padR = attCtl.tickPadR;
                                        const drawW = Math.max(1, width - padL - padR);

                                        ctx.fillStyle = attSlider.palette.text;
                                        ctx.font = "10px sans-serif";
                                        ctx.textBaseline = "top";

                                        const labelEvery = attCtl.effectiveLabelEvery;

                                        for (let i = 0; i < n; ++i) {
                                            const x = padL + i * (drawW / denom);

                                            // tick: ALWAYS draw
                                            const isEnd = (i === 0 || i === n - 1);
                                            const th = isEnd ? attCtl.endTickH : attCtl.midTickH;
                                            ctx.fillRect(x, 0, 1, th);

                                            // labels: auto density (plus ends)
                                            const showLabel = isEnd || (i % labelEvery === 0);
                                            if (!showLabel) continue;

                                            const val = attCtl.sFrom + i * attCtl.sStep;
                                            const t = String(val);
                                            const tw = ctx.measureText(t).width;

                                            let tx = x - tw / 2;
                                            if (tx < padL) tx = padL;
                                            if (tx + tw > width - padR) tx = width - padR - tw;

                                            ctx.fillText(t, tx, attCtl.labelY);
                                        }
                                    }

                                    // repaint when anything relevant changes
                                    Connections {
                                        target: attCtl
                                        function onStepsChanged() { tickCanvas.requestPaint(); }
                                        function onFromChanged() { tickCanvas.requestPaint(); }
                                        function onStepChanged() { tickCanvas.requestPaint(); }
                                        function onTickPadLChanged() { tickCanvas.requestPaint(); }
                                        function onTickPadRChanged() { tickCanvas.requestPaint(); }
                                        function onEndTickHChanged() { tickCanvas.requestPaint(); }
                                        function onMidTickHChanged() { tickCanvas.requestPaint(); }
                                        function onLabelYChanged() { tickCanvas.requestPaint(); }
                                        function onEffectiveLabelEveryChanged() { tickCanvas.requestPaint(); }
                                        function onMinLabelSpacingPxChanged() { tickCanvas.requestPaint(); }
                                    }
                                    onWidthChanged: requestPaint()
                                    Component.onCompleted: requestPaint()
                                }
                            }

                            function syncFromController() {
                                if (!controller || attCtl.sStep <= 0) return;
                                const v = Number(controller.attenuator);
                                const idx = Math.round((v - attCtl.sFrom) / attCtl.sStep);
                                attCtl.syncing = true;
                                attSlider.value = Math.max(0, Math.min(attCtl.steps - 1, idx));
                                attCtl.syncing = false;
                                tickCanvas.requestPaint();
                            }

                            Component.onCompleted: syncFromController()

                            Connections {
                                target: controller
                                function onAttenuatorChanged() { attCtl.syncFromController(); }
                            }
                        }


                        Label {
                            text: "Preamp"
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            visible: preCtl.visible
                        }
                        Item {
                            id: preCtl
                            Layout.fillWidth: true
                            implicitHeight: 40

                            readonly property var spec: controller ? controller.uiSpecs["preamp"] : null
                            visible: spec ? (spec.visible ?? true) : false

                            readonly property real sFrom: spec ? Number(spec.from ?? 0) : 0
                            readonly property real sTo:   spec ? Number(spec.to   ?? 0) : 0
                            readonly property real sStep: spec ? Math.max(1, Number(spec.step ?? 1)) : 1
                            readonly property string unit: spec ? String(spec.unit ?? "dB") : "dB"

                            readonly property int steps: {
                                if (sTo === sFrom) return 1;
                                return Math.floor(Math.abs((sTo - sFrom) / sStep)) + 1; // inclusive
                            }

                            // --- Tick/label styling ---
                            property int endTickH: 8
                            property int midTickH: 4
                            property int tickPadL: 2
                            property int tickPadR: 2
                            property int labelY: 10

                            // Auto label density (ONLY labels)
                            // Optional override: spec.labelEvery > 0 forces it
                            readonly property int labelEvery: spec ? Math.max(0, Number(spec.labelEvery ?? 0)) : 0
                            // Minimum pixel spacing between *labels* (auto mode)
                            readonly property int minLabelSpacingPx: spec ? Math.max(4, Number(spec.minLabelSpacingPx ?? 26)) : 26

                            readonly property int effectiveLabelEvery: {
                                if (labelEvery > 0) return labelEvery;
                                if (steps <= 1 || preCanvas.width <= 0) return 1;

                                const drawW = Math.max(1, preCanvas.width - tickPadL - tickPadR);
                                const pxPerStep = drawW / (steps - 1);
                                return Math.max(1, Math.ceil(minLabelSpacingPx / Math.max(1, pxPerStep)));
                            }

                            property bool syncing: false

                            Column {
                                anchors.fill: parent
                                spacing: 2

                                Slider {
                                    id: preSlider
                                    width: parent.width

                                    from: 0
                                    to: Math.max(0, preCtl.steps - 1)
                                    stepSize: 1
                                    snapMode: Slider.SnapAlways

                                    onMoved: {
                                        if (!controller || preCtl.syncing) return;
                                        controller.preamp = preCtl.sFrom + Math.round(value) * preCtl.sStep;
                                    }

                                    HoverHandler { id: hPre }
                                    ToolTip.visible: hPre.hovered
                                    ToolTip.delay: 200
                                    ToolTip.text: (preCtl.sFrom + Math.round(preSlider.value) * preCtl.sStep) + preCtl.unit
                                }

                                Canvas {
                                    id: preCanvas
                                    width: preSlider.width
                                    height: 20

                                    onPaint: {
                                        const ctx = getContext("2d");
                                        ctx.clearRect(0, 0, width, height);

                                        const n = preCtl.steps;
                                        if (n <= 1 || width <= 2) return;

                                        const denom = n - 1;

                                        const padL = preCtl.tickPadL;
                                        const padR = preCtl.tickPadR;
                                        const drawW = Math.max(1, width - padL - padR);

                                        ctx.fillStyle = preSlider.palette.text;
                                        ctx.font = "10px sans-serif";
                                        ctx.textBaseline = "top";

                                        const labelEvery = preCtl.effectiveLabelEvery;

                                        for (let i = 0; i < n; ++i) {
                                            const x = padL + i * (drawW / denom);

                                            // tick: ALWAYS draw
                                            const isEnd = (i === 0 || i === n - 1);
                                            const th = isEnd ? preCtl.endTickH : preCtl.midTickH;
                                            ctx.fillRect(x, 0, 1, th);

                                            // labels: auto density (plus ends)
                                            const showLabel = isEnd || (i % labelEvery === 0);
                                            if (!showLabel) continue;

                                            const val = preCtl.sFrom + i * preCtl.sStep;
                                            const t = String(val);
                                            const tw = ctx.measureText(t).width;

                                            let tx = x - tw / 2;
                                            if (tx < padL) tx = padL;
                                            if (tx + tw > width - padR) tx = width - padR - tw;

                                            ctx.fillText(t, tx, preCtl.labelY);
                                        }
                                    }

                                    Connections {
                                        target: preCtl
                                        function onStepsChanged() { preCanvas.requestPaint(); }
                                        function onSFromChanged() { preCanvas.requestPaint(); }
                                        function onSTepChanged() { preCanvas.requestPaint(); }
                                        function onTickPadLChanged() { preCanvas.requestPaint(); }
                                        function onTickPadRChanged() { preCanvas.requestPaint(); }
                                        function onEndTickHChanged() { preCanvas.requestPaint(); }
                                        function onMidTickHChanged() { preCanvas.requestPaint(); }
                                        function onLabelYChanged() { preCanvas.requestPaint(); }
                                        function onEffectiveLabelEveryChanged() { preCanvas.requestPaint(); }
                                        function onMinLabelSpacingPxChanged() { preCanvas.requestPaint(); }
                                    }
                                    onWidthChanged: requestPaint()
                                    Component.onCompleted: requestPaint()
                                }
                            }

                            function syncFromController() {
                                if (!controller || preCtl.sStep <= 0) return;
                                const v = Number(controller.preamp);
                                const idx = Math.round((v - preCtl.sFrom) / preCtl.sStep);
                                preCtl.syncing = true;
                                preSlider.value = Math.max(0, Math.min(preCtl.steps - 1, idx));
                                preCtl.syncing = false;
                                preCanvas.requestPaint();
                            }

                            Component.onCompleted: syncFromController()

                            Connections {
                                target: controller
                                function onPreampChanged() { preCtl.syncFromController(); }
                            }
                        }

                        Item {
                            id: antCtl
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitHeight: antRow.implicitHeight
                            Layout.preferredHeight: antRow.implicitHeight
                            Layout.minimumHeight: antRow.implicitHeight

                            readonly property var spec: controller ? controller.uiSpecs["antenna"] : null
                            visible: spec ? (spec.visible ?? true) : false
                            readonly property var options: spec ? (spec.options ?? []) : []

                            RowLayout {
                                id: antRow
                                anchors.fill: parent
                                spacing: 8

                                ButtonGroup { id: antGroup }

                                Repeater {
                                    model: antCtl.options
                                    delegate: Button {
                                        required property var modelData
                                        text: modelData.text
                                        checkable: true
                                        ButtonGroup.group: antGroup
                                        checked: controller ? (controller.antenna === modelData.value) : false
                                        onClicked: if (controller) controller.antenna = modelData.value

                                        // fit to content
                                        implicitWidth: Math.max(24, contentItem ? (contentItem.implicitWidth + leftPadding + rightPadding) : 34)
                                    }
                                }

                                Item { Layout.fillWidth: true } // pushes RX to the right

                                CheckBox {
                                    text: "RX"
                                    checked: controller ? controller.rxAntenna : false
                                    onToggled: if (controller) controller.rxAntenna = checked
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                        }


                        Item {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitHeight: enableRow.implicitHeight

                            RowLayout {
                                id: enableRow
                                anchors.fill: parent
                                spacing: 8

                                Label {
                                    text: "Scope enabled"
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    Layout.preferredWidth: 110   // match your label column width
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

