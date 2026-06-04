// scope.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

//pragma ComponentBehavior: Bound

Control {

    id: root

    readonly property var hostWin: ApplicationWindow.window
    property int receiverIndex: 0
    property var controller: null
    property alias bandPanelOpen: bandPanel.open
    property alias sidePanelOpen: sidePanel.open
    property bool restoringReceiverUiState: true
    property bool receiverFullScreen: false

    signal requestDetach(var globalPos)
    signal fullScreenRequested(bool enabled)
    signal spectrumProcessingTime(real ms)
    signal waterfallProcessingTime(real ms)
    padding: 1
    clip: false

    function indexFromValue(cb, v) {
        if (!cb || v === undefined || v === null)
            return -1

        var sv = String(v)

        for (var i = 0; i < cb.count; ++i) {
            var it = (cb.model && cb.model.get) ? cb.model.get(i) : cb.model[i]
            if (!it)
                continue

            var iv = it.value
            if (iv === v || String(iv) === sv)
                return i
        }
        return -1
    }

    function isCenterScopeMode(v) {
        // Icom/Kenwood use numeric 0 for center mode. Yaesu CAT scope modes
        // are ASCII values: '0', '3', and '4' are center variants.
        return v === 0 || v === 48 || v === 51 || v === 52
    }

    function scopeDisplayKey(mainName, subName) {
        return controller && controller.receiverIndex === 0 ? mainName : subName
    }

    function setInterfaceOption(key, value) {
        if (MainController.settings)
            MainController.settings.setOption(key, value)
    }

    function comboTextPreferredWidth(model, textRole, minimumWidth, maximumWidth) {
        let longest = minimumWidth
        if (!model)
            return minimumWidth

        const count = model.count !== undefined ? model.count : model.length
        for (let i = 0; i < count; ++i) {
            const item = model.get ? model.get(i) : model[i]
            if (!item)
                continue

            const textLength = String(item[textRole] ?? "").length
            longest = Math.max(longest, textLength * 7 + 34)
        }

        return Math.min(Math.ceil(longest), maximumWidth)
    }

    component AboveToolTip: ToolTip {
        delay: 300
        timeout: 5000
        x: parent ? (parent.width - width) / 2 : 0
        y: -implicitHeight - 4
    }

    function receiverSetting(key, fallback) {
        return MainController.settings
                ? MainController.settings.receiverSetting(root.receiverIndex, key, fallback)
                : fallback
    }

    function boolReceiverSetting(key, fallback) {
        return boolReceiverSettingValue(receiverSetting(key, fallback), fallback)
    }

    function boolReceiverSettingValue(value, fallback) {
        if (typeof value === "boolean")
            return value
        if (typeof value === "number")
            return value !== 0
        if (typeof value === "string") {
            const s = value.trim().toLowerCase()
            if (s === "true" || s === "1" || s === "yes" || s === "on")
                return true
            if (s === "false" || s === "0" || s === "no" || s === "off" || s === "")
                return false
        }
        return fallback === undefined ? false : boolReceiverSettingValue(fallback, false)
    }

    function saveReceiverSetting(key, value) {
        if (!root.restoringReceiverUiState && MainController.settings)
            MainController.settings.saveReceiverSetting(root.receiverIndex, key, value)
    }

    background: Rectangle {
        color: palette.window
        radius: 0
        border.width: 1
        border.color: Qt.darker(palette.window, 1.4)
        antialiasing: true
    }

    contentItem: Item{
        id: receiverRoot
        anchors.fill: parent
        clip: true

        WheelHandler
        {
            id: wh
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                if (!controller) return;
                controller.onWheelTune(event.angleDelta.y, event.modifiers);
                event.accepted = true;
            }
        }

        ColumnLayout {
            id: contentLayout
            spacing: 2
            anchors.fill: parent
            anchors.leftMargin: bandPanel.locked && bandPanel.open ? bandPanel.width : 0
            anchors.rightMargin: sidePanel.locked && sidePanel.open ? sidePanel.width : 0

            Behavior on anchors.leftMargin { NumberAnimation { duration: 140 } }
            Behavior on anchors.rightMargin { NumberAnimation { duration: 140 } }

            Item {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: 28

                Meter {
                    id: smeter
                    anchors.left: parent.left
                    anchors.leftMargin: 36
                    anchors.top: parent.top
                    anchors.topMargin: 2
                    width: 300
                    height: 26
                    meterType: root.controller ? root.controller.meterType : 0
                    // only set the "current" level; MeterItem does peak/avg itself
                    current: root.controller ? root.controller.meter : 0
                    scaleTextColor: root.palette.windowText
                    scaleLineColor: root.palette.windowText
                    scaleHighTextColor: MainController.settings.options["Color.MeterHighScale"]
                    scaleHighLineColor: MainController.settings.options["Color.MeterHighScale"]

                    drawLabels: true
                    function applyExtremities() {
                        const type = root.controller ? root.controller.meterType : 0
                        const ext = MainController.optionalMeterExtremities(type)
                        if (ext.valid) {
                            smeter.setMeterExtremities(ext.low, ext.high, ext.red)
                            smeter.warningText = ""
                        } else if (type === 1 || type === 17) {
                            smeter.setMeterExtremities(-54, 60, 0)
                            smeter.warningText = ""
                        } else if (type === 3) {
                            smeter.setMeterExtremities(1, 6, 3)
                            smeter.warningText = ""
                        } else {
                            smeter.clearMeterExtremities()
                            smeter.warningText = ext.calibrationMissing ? qsTr("No calibration found") : ""
                        }
                    }
                    Component.onCompleted: applyExtremities()
                    onMeterTypeChanged: applyExtremities()
                }

                Popup {
                    id: meterTypePopup
                    modal: false
                    x: 0
                    y: 0

                    contentItem: ComboBox {
                        id: combo
                        model: [
                            { text:qsTr("None"), value: 0 },          // meterNone
                            { text:qsTr("S-Meter"), value: 1 },   // adjust enum values to match meter_t
                            { text:qsTr("Sub S-Meter"), value: 2 },   // adjust enum values to match meter_t
                            { text:qsTr("SWR"), value: 5 },
                            { text:qsTr("ALC"), value: 4 },
                            { text:qsTr("Compression"), value: 8 },
                            { text:qsTr("Voltage"), value: 6 },
                            { text:qsTr("Current"), value: 7 },
                            { text:qsTr("Center"), value: 9 },
                            { text:qsTr("TxRxAudio"), value: 10 },
                            { text:qsTr("RxAudio"), value: 11 },
                            { text:qsTr("TxAudio"), value: 12 }
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
                        color: Qt.darker(palette.window, 1.05)

                        // keep it out of the meter area when things get tight
                        readonly property int leftLimit: smeter.x + smeter.width + 8
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
                        color: root.palette.windowText
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

                ToolButton {
                    id: bandDrawerButton
                    text: qsTr("\u2630")
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 3
                    width: 28
                    height: 24
                    z: 10
                    onClicked: {
                        bandPanel.open = !bandPanel.open
                        bandPanel.slide = bandPanel.open ? bandPanel.width : 0
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
                    pbtLow: controller ? controller.pbtLow : 0
                    pbtHigh: controller ? controller.pbtHigh : 0
                    floor: controller ? controller.plotFloor : 0
                    ceiling: controller ? controller.plotCeiling : 160
                    colors: controller ? controller.colors : null
                    center: controller ? controller.frequencyA : null
                    bands: controller && controller.activeBands
                    underlayMode: MainController.settings ? Number(MainController.settings.options["Interface.UnderlayMode"]) : 2
                    peakDecay: MainController.settings ? Number(MainController.settings.options["Interface.PeakDecay"]) : 50
                    underlayBufferSize: MainController.settings ? Number(MainController.settings.options["Interface.UnderlayBufferSize"]) : 80
                    onTuneRequested: function(freq) {
                        controller.setFrequencyA(freq*1000000.0,true)   // or controller.setFrequencyHz(freq)
                        controller.frequencyAChanged() // Signal the controller that frequency has changed
                    }
                    onPassbandResizeRequested: function(lowFreq, highFreq) {
                        if (controller)
                            controller.resizePassband(lowFreq, highFreq)
                    }
                    onPbtDragRequested: function(action, deltaMHz) {
                        if (controller)
                            controller.dragPbt(action, deltaMHz)
                    }
                    onPbtResetRequested: {
                        if (controller)
                            controller.resetPbt()
                    }
                    onProcessingTimeNs: function(ns) {
                        root.spectrumProcessingTime(ns / 1000000.0)
                    }

                }

                WaterfallItem {
                    id: waterfall
                    controller: root.controller
                    SplitView.fillWidth: true
                    SplitView.preferredHeight: (splitView.height * 0.5) - 15
                    theme: controller ? controller.theme : 1
                    length: controller ? controller.waterfallLength : 160
                    floor: controller ? controller.plotFloor : 0
                    ceiling: controller ? controller.plotCeiling : 160
                    smooth: controller ? (controller.waterfallSmooth || controller.waterfallAntiAlias) : true
                    onTuneRequested: function(freq) {
                        controller.setFrequencyA(freq*1000000.0,true)   // or controller.setFrequencyHz(freq)
                        controller.frequencyAChanged() // Signal the controller that frequency has changed
                    }
                    onProcessingTimeNs: function(ns) {
                        root.waterfallProcessingTime(ns / 1000000.0)
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
                                Layout.fillHeight: true
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                                minFrequency: controller ? controller.freqDisplay["min"] : 0
                                maxFrequency: controller ? controller.freqDisplay["max"] : 4000000000
                                freqMinStep: controller ? controller.freqDisplay["minStep"] : 1
                                freqDigits: controller ? controller.freqDisplay["digits"] : 10

                                unit: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.FrequencyUnits"]
                                      : 0

                                gsep: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.DecimalSeparator"]
                                      : 0

                                dsep: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.GroupSeparator"]
                                      : 0

                                frequency: controller ? controller.frequencyA : 0

                                visible: controller && ((controller.uiFlags & ReceiverController.ShowVFOA) !== 0)

                                onNewFrequency: function(freq) {
                                    controller.setFrequencyA(freq, true)
                                    controller.frequencyAChanged()
                                }
                            }


                            Button {
                                id: vfoButton;
                                text: controller && controller.vfoBSelected ? "VFOB" : "VFOA";
                                checkable: true
                                checked: controller ? controller.vfoBSelected : false
                                visible: controller && (controller.uiFlags & ReceiverController.ShowVfoButton) !== 0
                                onClicked: if (controller) controller.selectVfoB(checked)
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
                                        text: qsTr("A↔B");
                                        enabled: true
                                        visible: controller && (controller.uiFlags & ReceiverController.ShowSwapABButton) !== 0
                                        onClicked: if (controller) controller.swapVfoAB()
                                    }

                                    Button {
                                        id: equalsABButton;
                                        text: qsTr("A=B");
                                        enabled: true
                                        visible: controller && (controller.uiFlags & ReceiverController.ShowEqualsABButton) !== 0
                                        onClicked: if (controller) controller.equalizeVfoAB()
                                    }

                                    Button {
                                        id: vmButton;
                                        text: qsTr("V/M");
                                        checkable: true
                                        checked: controller ? controller.memoryMode : false
                                        visible: controller && (controller.uiFlags & ReceiverController.ShowVMButton) !== 0
                                        onClicked: if (controller) controller.setMemoryMode(checked)
                                    }

                                    Button {
                                        id: satButton;
                                        text: qsTr("SAT");
                                        checkable: true
                                        checked: controller ? controller.satelliteMode : false
                                        visible: controller && (controller.uiFlags & ReceiverController.ShowSatButton) !== 0
                                        onClicked: if (controller) controller.setSatelliteMode(checked)
                                    }

                                    Button {
                                        id: splitButton;
                                        text: qsTr("SPLIT");
                                        checkable: true
                                        checked: controller ? controller.splitEnabled : false
                                        visible: controller && (controller.uiFlags & ReceiverController.ShowSplitButton) !== 0
                                        onClicked: if (controller) controller.setSplitEnabled(checked)
                                    }
                                }
                            }

                            // Right freq control
                            FreqCtrlQuick {
                                id: rightFreq
                                objectName: "rightFreq"
                                Layout.preferredWidth: 260
                                Layout.fillHeight: true
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                                freqDigits: controller ? controller.freqDisplay["digits"] : 0
                                minFrequency: controller ? controller.freqDisplay["min"] : 0
                                maxFrequency: controller ? controller.freqDisplay["max"] : 0
                                freqMinStep: controller ? controller.freqDisplay["minStep"] : 0

                                unit: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.FrequencyUnits"]
                                      : 0

                                gsep: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.DecimalSeparator"]
                                      : 0

                                dsep: (controller && MainController.settings && MainController.settings.options)
                                      ? MainController.settings.options["Interface.GroupSeparator"]
                                      : 0

                                frequency: controller ? controller.frequencyB : 0

                                visible: controller && ((controller.uiFlags & ReceiverController.ShowVFOB) !== 0)

                                onNewFrequency: function(freq) {
                                    controller.setFrequencyB(freq, true)
                                    controller.frequencyBChanged()
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
                                currentIndex: controller ? indexFromValue(scopeModeCombo, controller.scopeMode) : -1

                                onActivated: controller.scopeMode = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 82, 125)
                                AboveToolTip {
                                    text: qsTr("Scope display mode")
                                    visible: scopeModeCombo.hovered
                                }
                            }

                            ComboBox {
                                id: scopeSpanCombo
                                readonly property var spec: controller ? controller.uiSpecs["scopeSpans"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: (spec ? (spec.visible ?? true) : false) && root.isCenterScopeMode(scopeModeCombo.currentValue)
                                currentIndex: controller ? indexFromValue(scopeSpanCombo,controller.scopeSpan) : -1
                                onActivated: controller.scopeSpan = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 58, 95)
                                AboveToolTip {
                                    text: qsTr("Center-mode span")
                                    visible: scopeSpanCombo.hovered
                                }
                            }

                            ComboBox {
                                id: scopeEdgeCombo
                                readonly property var spec: controller ? controller.uiSpecs["scopeEdges"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: (spec ? (spec.visible ?? true) : false) && !root.isCenterScopeMode(scopeModeCombo.currentValue)
                                currentIndex: controller ? indexFromValue(scopeEdgeCombo,controller.scopeEdge) : -1
                                onActivated: controller.scopeEdge = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 92, 140)
                                AboveToolTip {
                                    text: qsTr("Fixed edge range")
                                    visible: scopeEdgeCombo.hovered
                                }
                            }

                            Button {
                                id: customEdgeButton
                                objectName: "customEdge"
                                visible: !root.isCenterScopeMode(scopeModeCombo.currentValue)
                                text: qsTr("Custom Edge")
                                onClicked: customEdgeDialog.open()
                                Layout.preferredWidth: 90
                            }

                            Button {
                                id: toFixedButton
                                objectName: "toFixed"
                                text: qsTr("To Fixed")
                                visible: root.isCenterScopeMode(scopeModeCombo.currentValue)
                                onClicked: edgeDialog.open()
                                Layout.preferredWidth: 70
                            }

                            Button {
                                id: holdButton
                                objectName: "hold"
                                text: qsTr("Hold")
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
                                id: modeCombo
                                readonly property var spec: controller ? controller.uiSpecs["modes"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: spec ? (spec.visible ?? true) : false
                                currentIndex: controller ? indexFromValue(modeCombo,controller.mode) : -1
                                onActivated: controller.mode = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 58, 90)
                                AboveToolTip {
                                    text: qsTr("Operating mode")
                                    visible: modeCombo.hovered
                                }
                            }

                            ComboBox {
                                id: dataModeCombo
                                readonly property var spec: controller ? controller.uiSpecs["dataModes"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: spec ? (spec.visible ?? true) : false
                                currentIndex: controller ? indexFromValue(dataModeCombo,controller.dataMode) : -1
                                onActivated: controller.dataMode = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 64, 105)
                                AboveToolTip {
                                    text: qsTr("Data mode")
                                    visible: dataModeCombo.hovered
                                }
                            }

                            ComboBox {
                                id: filterCombo
                                readonly property var spec: controller ? controller.uiSpecs["filters"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: spec ? (spec.visible ?? true) : false
                                currentIndex: controller ? indexFromValue(filterCombo,controller.filter) : -1
                                onActivated: controller.filter = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 78, 110)
                                AboveToolTip {
                                    text: qsTr("IF filter selection")
                                    visible: filterCombo.hovered
                                }
                            }

                            ComboBox {
                                id: filterShapeCombo
                                readonly property var spec: controller ? controller.uiSpecs["filterShapes"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: spec ? (spec.visible ?? true) : false
                                enabled: spec ? (spec.enabled ?? true) : false
                                currentIndex: controller ? indexFromValue(filterShapeCombo,controller.filterShape) : -1
                                onActivated: controller.filterShape = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 66, 95)
                                AboveToolTip {
                                    text: qsTr("Filter shape")
                                    visible: filterShapeCombo.hovered
                                }
                            }

                            ComboBox {
                                id: roofingCombo
                                readonly property var spec: controller ? controller.uiSpecs["roofingFilters"] : null
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                visible: spec ? (spec.visible ?? true) : false
                                currentIndex: controller ? indexFromValue(roofingCombo,controller.roofing) : -1
                                onActivated: controller.roofing = currentValue
                                Layout.preferredWidth: root.comboTextPreferredWidth(model, textRole, 70, 105)
                                AboveToolTip {
                                    text: qsTr("Roofing filter")
                                    visible: roofingCombo.hovered
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Button {
                                id: clearPeaksButton
                                objectName: "clearPeaks"
                                text: qsTr("Clear Peaks")
                                onClicked: spectrum.clearPeaks()
                                Layout.preferredWidth: 80

                            }
                        }
                    }
                }
            }
        }

        // 2) Overlay band panel (receiver-local “drawer”), sibling of the layout
        Item {
            id: bandPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            property var controller: root.controller

            property bool open: false
            property real slide: 0    // 0..width

            // Desired button size + spacing (keep in sync with bandGrid values)
            property int cellW: 54
            property int gap: 6
            property int pad: 8

            property bool autoHide: true
            property bool locked: false
            // computed by grid (see below)
            property int cols: 1

            readonly property int footerH: 40
            readonly property int minW: 60
            readonly property int maxW: Math.round(receiverRoot.width * 0.70)

            // width is exactly what's needed for cols, clamped
            width: Math.max(minW, Math.min(maxW, cols * cellW + Math.max(0, cols - 1) * gap + pad * 2))

            readonly property int panelW: Math.min(320, Math.max(80, Math.round(receiverRoot.width * 0.35)))
            height: receiverRoot.height

            x: locked && open ? 0 : -width + slide
            Behavior on slide { NumberAnimation { duration: 140 } }

            // keep slide consistent when open toggles / width changes
            onOpenChanged: {
                slide = open ? width : 0
                root.saveReceiverSetting("BandDrawerOpen", open)
            }
            onWidthChanged: slide = open ? width : 0
            onLockedChanged: {
                if (locked) {
                    open = true
                    slide = width
                    autoHide = false
                }
                root.saveReceiverSetting("BandDrawerLocked", locked)
            }

            Rectangle {
                anchors.fill: parent
                z: -1                      // sit behind header/content
                color: palette.window
                opacity: 1.0
                border.color: palette.highlight
                border.width: 1
            }

            ButtonGroup {
                id: bandGroup
                exclusive: true
            }

            // ... header ...
            Item {
                id: bandHeader
                height: 30
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top

                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 6
                    icon.name: "window-close"
                    onClicked: {
                        bandPanel.locked = false
                        bandPanel.open = false
                        bandPanel.slide = 0
                    }
                }

                ToolButton {
                    id: bandLockButton
                    checkable: true
                    checked: bandPanel.locked
                    text: checked ? "\uD83D\uDD12" : "\uD83D\uDD13"
                    anchors.right: parent.right
                    anchors.rightMargin: 36
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: bandPanel.locked = checked
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("Unlock drawer") : qsTr("Lock drawer")
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 72
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Bands")
                    font.bold: true
                    elide: Text.ElideRight
                }
            }

            // Footer (fixed, locked to bottom)
            Item {
                id: bandFooter
                height: 40
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"   // or root.palette.window
                    // Optional divider line:
                    // border.width: 1; border.color: Qt.darker(root.palette.window, 1.2)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: bandPanel.pad
                    anchors.rightMargin: bandPanel.pad
                    spacing: 8

                    ComboBox {
                        id: bandsBsrReg
                        Layout.fillWidth: true
                        enabled: true
                        // Example model (use your uiSpecs like everything else)
                        model: [
                            { text:qsTr("BSR 1"), value: 1 },
                            { text:qsTr("BSR 2"), value: 2 },
                            { text:qsTr("BSR 3"), value: 3 }
                        ]
                        textRole: "text"
                        valueRole: "value"
                        // If you need initial selection:
                        // currentIndex: root.controller ? indexFromValue(regionCombo, root.controller.bandRegion) : -1
                        onActivated: if (root.controller) root.controller.bsrReg = currentValue
                    }

                    Button {
                        text: qsTr("Store")
                        Layout.preferredWidth: 50
                        onClicked: {
                            // do whatever you need
                            root.controller.storeBsr();
                            // e.g. root.controller.setBandRegion(regionCombo.currentValue)
                        }
                    }
                }
            }

            // content
            Flickable {
                id: bandFlick
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: bandHeader.bottom
                anchors.bottom: bandFooter.top
                clip: true

                property int pad: bandPanel.pad

                contentWidth: width
                contentHeight: bandGrid.implicitHeight + pad * 2

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                GridLayout {
                    id: bandGrid
                    x: bandFlick.pad
                    y: bandFlick.pad
                    width: bandFlick.width - bandFlick.pad * 2

                    // model from uiSpecs (Qt5-safe)
                    readonly property var spec: (root.controller && root.controller.uiSpecs)
                        ? ((root.controller.uiSpecs.bands !== undefined)
                            ? root.controller.uiSpecs.bands
                            : root.controller.uiSpecs["bands"])
                        : null

                    readonly property var m: (spec && spec.model !== undefined) ? spec.model : []

                    property int cellW: bandPanel.cellW
                    property int cellH: 34
                    property int gap: bandPanel.gap

                    readonly property int n: (m && m.length !== undefined) ? m.length : 0

                    // available height inside the flick area (use height, not width)
                    readonly property int availH: Math.max(0, bandFlick.height - bandFlick.pad * 2)
                    readonly property int maxRowsByH: Math.max(1, Math.floor((availH + gap) / (cellH + gap)))

                    // choose cols so we use height (more rows, fewer columns)
                    readonly property int colsByH: Math.max(2, Math.ceil(n / maxRowsByH))

                    columns: colsByH

                    onColumnsChanged: bandPanel.cols = columns
                    Component.onCompleted: bandPanel.cols = columns

                    columnSpacing: gap
                    rowSpacing: gap

                    Repeater {
                        model: bandGrid.m
                        delegate: Button {
                            text: (modelData && modelData.text !== undefined) ? modelData.text
                                 : ((modelData && modelData.name !== undefined) ? modelData.name : "")

                            Layout.preferredWidth: bandGrid.cellW
                            Layout.preferredHeight: bandGrid.cellH

                            ButtonGroup.group: bandGroup
                            checkable: true

                            checked: bandPanel.controller ? (bandPanel.controller.band === modelData.value) : false

                            onClicked: {
                                if (bandPanel.controller)
                                    bandPanel.controller.band = modelData.value
                                if (bandPanel.autoHide && !bandPanel.locked)
                                    bandPanel.open = false
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            id: bandHandle
            width: 10
            anchors.left: parent.left
            anchors.top: parent.top
            height: receiverRoot.height
            color: "transparent"
            z: 10000

            visible: !bandPanel.open
            enabled: !bandPanel.open

            DragHandler {
                onActiveChanged: {
                    if (active) {
                        bandPanel.open = true
                    } else {
                        bandPanel.open = (bandPanel.slide > bandPanel.width * 0.5)
                        bandPanel.slide = bandPanel.open ? bandPanel.width : 0
                    }
                }
                onTranslationChanged: {
                    bandPanel.slide = Math.max(0, Math.min(bandPanel.width, translation.x))
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    bandPanel.open = !bandPanel.open
                    bandPanel.slide = bandPanel.open ? bandPanel.width : 0
                }
            }
        }

        // --- right-side drawer-like panel (overlay) ---
        Item {
            id: sidePanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            property bool open: false
            property real slide: 0
            property bool locked: false
            property var controller: root.controller

            // FIXED width
            readonly property int panelW: 240
            width: panelW
            height: receiverRoot.height

            // Right edge slide
            x: locked && open ? receiverRoot.width - width : receiverRoot.width - slide
            Behavior on slide { NumberAnimation { duration: 140 } }

            onOpenChanged: {
                slide = open ? width : 0
                root.saveReceiverSetting("ControlDrawerOpen", open)
            }
            onWidthChanged: slide = open ? width : 0
            onLockedChanged: {
                if (locked) {
                    open = true
                    slide = width
                }
                root.saveReceiverSetting("ControlDrawerLocked", locked)
            }

            z: 9999
            clip: true
            visible: slide > 0.5

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const maxY = Math.max(0, sideFlick.contentHeight - sideFlick.height)
                    if (maxY > 0) {
                        const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 3
                        sideFlick.contentY = Math.max(0, Math.min(maxY, sideFlick.contentY - delta))
                    }
                    event.accepted = true
                }
            }

            Rectangle {
                anchors.fill: parent
                z: -1
                color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, 1.0)
                border.color: palette.highlight
                border.width: 1
            }

            // Header
            Item {
                id: sideHeader
                height: 30
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top

                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 6
                    icon.name: "window-close"
                    onClicked: {
                        sidePanel.locked = false
                        sidePanel.open = false
                        sidePanel.slide = 0
                    }
                }

                ToolButton {
                    id: sideLockButton
                    checkable: true
                    checked: sidePanel.locked
                    text: checked ? "\uD83D\uDD12" : "\uD83D\uDD13"
                    anchors.right: parent.right
                    anchors.rightMargin: 36
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: sidePanel.locked = checked
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("Unlock drawer") : qsTr("Lock drawer")
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 72
                    anchors.verticalCenter: parent.verticalCenter
                    text: controller ? controller.drawerTitle : ""
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // Content fills the panel
            Flickable {
                id: sideFlick
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: sideHeader.bottom
                anchors.bottom: parent.bottom
                clip: true

                readonly property int pad: 12
                readonly property int labelColW: 80   // <-- choose what looks right

                contentWidth: width
                contentHeight: drawerGrid.implicitHeight + pad * 2

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                GridLayout {
                    id: drawerGrid
                    x: sideFlick.pad
                    y: sideFlick.pad
                    width: sideFlick.width - sideFlick.pad * 2

                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 8

                    // --- IMPORTANT: force column 0 width ---
                    // Put this on EVERY Label in column 0:

                    // Put this on EVERY control in column 1:
                    // Layout.fillWidth: true

                    Label {
                        text: qsTr("Full screen")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    CheckBox {
                        id: receiverFullScreenCheck
                        Layout.fillWidth: true
                        checked: root.receiverFullScreen
                        onToggled: {
                            if (root.receiverFullScreen === checked)
                                return
                            root.receiverFullScreen = checked
                            root.saveReceiverSetting("DetachedFullScreen", checked)
                            root.fullScreenRequested(checked)
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Use a borderless full-screen detached receiver")
                    }

                    // === Ref slider ===
                    Label {
                        text: qsTr("Ref")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        value: controller ? controller.ref : 0
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
                        text: qsTr("Length")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: lengthSlider
                        from: 10
                        to: 1024
                        stepSize: 16
                        value: controller ? controller.waterfallLength : 160
                        //Layout.preferredWidth: 120
                        Layout.fillWidth: true
                        onMoved: {
                            if (controller) {
                                const rounded = Math.round(value)
                                controller.waterfallLength = rounded
                                saveReceiverSetting("WFLength", rounded)
                            }
                        }
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
                        text: qsTr("Ceiling")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: ceilingSlider
                        from: 0
                        to: 255
                        value: controller ? controller.plotCeiling : 160
                        //Layout.preferredWidth: 120
                        Layout.fillWidth: true
                        onMoved:  {
                            if (controller) {
                                const rounded = Math.round(value)
                                controller.plotCeiling = rounded
                                saveReceiverSetting("PlotCeiling", rounded)
                            }
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
                        text: qsTr("Floor")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: floorSlider
                        from: 0
                        to: 255
                        value: controller ? controller.plotFloor : 0
                        //Layout.preferredWidth: 120
                        Layout.fillWidth: true
                        onMoved:  {
                            if (controller) {
                                const rounded = Math.round(value)
                                controller.plotFloor = rounded
                                saveReceiverSetting("PlotFloor", rounded)
                            }
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
                        text: qsTr("Speed")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: speedCombo
                        readonly property var spec: controller ? controller.uiSpecs["speeds"] : null
                        model: spec ? spec.model : []
                        textRole: spec ? spec.textRole : "text"
                        valueRole: spec ? spec.valueRole : "value"
                        visible: spec ? (spec.visible ?? true) : false
                        currentIndex: controller ? indexFromValue(speedCombo,controller.speed) : -1
                        onActivated: controller.speed = currentValue
                        Layout.fillWidth: true
                        //Layout.preferredWidth: 120
                    }

                    // === Theme combobox ===
                    Label {
                        text: qsTr("Theme")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: themeCombo
                        readonly property var spec: controller ? controller.uiSpecs["themes"] : null
                        model: spec ? spec.model : []
                        textRole: spec ? spec.textRole : "text"
                        valueRole: spec ? spec.valueRole : "value"
                        visible: spec ? (spec.visible ?? true) : false
                        currentIndex: controller ? indexFromValue(themeCombo,controller.theme) : -1
                        onActivated: {
                            if (controller) {
                                controller.theme = currentValue
                                saveReceiverSetting("WFTheme", currentValue)
                            }
                        }
                        Layout.fillWidth: true
                        //Layout.preferredWidth: 120
                    }

                    // === PBT Inner slider ===
                    Label {
                        text: qsTr("PBT Inner")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        value: controller ? controller.pbtInner : 0
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
                        text: qsTr("PBT Outer")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        value: controller ? controller.pbtOuter : 0
                        onMoved: controller.pbtOuter = value
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
                        text: qsTr("IF Shift")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    RowLayout {
                        //Layout.preferredWidth: 120
                        Layout.fillWidth: true
                        spacing: 2

                        Slider {
                            id: ifShiftSlider
                            objectName: "ifShift"
                            value: controller ? controller.ifShift : 0
                            onMoved: controller.ifShift = value
                            Layout.fillWidth: true
                            // onValueChanged: backend.ifShift = value
                        }

                        Button {
                            text: qsTr("R")
                            Layout.preferredWidth: 20
                            onClicked: ifShiftSlider.value = 0
                        }
                        implicitHeight: 25
                    }

                    // === Filter width slider ===
                    Label {
                        text: qsTr("Filter width")
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: filterWidthSlider
                        objectName: "filterWidth"
                        value: controller ? controller.filterWidth : 0
                        onMoved: controller.filterWidth = value
                        //Layout.preferredWidth: 120
                        Layout.fillWidth: true
                        // onValueChanged: backend.filterWidth = value
                    }

                    // === NB checkbox + slider ===
                    RowLayout {
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        spacing: 0

                        Label {
                            text: qsTr("NB")
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
                        Layout.preferredWidth: sideFlick.labelColW
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        spacing: 0

                        Label {
                            text: qsTr("NR")
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

                    Item {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        implicitHeight: extraControlsRow.implicitHeight

                        RowLayout {
                            id: extraControlsRow
                            anchors.fill: parent
                            spacing: 8

                            CheckBox {
                                // IP Plus
                                id: ipPlusCheckBox
                                text: qsTr("IP+")
                                checked: controller ? controller.ipPlus : false
                                onCheckedChanged: {
                                    if (controller)
                                        controller.ipPlus = checked
                                }
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            }

                            CheckBox {
                                // Manual Notch
                                id: mnCheckBox
                                text: qsTr("MN")
                                checked: controller ? controller.mn : false
                                onCheckedChanged: {
                                    if (controller)
                                        controller.mn = checked
                                }
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            }

                            CheckBox {
                                // Digi-Sel
                                id: dsCheckBox
                                text: qsTr("DS")
                                checked: controller ? controller.ds : false
                                onCheckedChanged: {
                                    if (controller)
                                        controller.ds = checked
                                }
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            }
                        }
                    }

                    Label {
                        text: qsTr("RF Gain")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        text: qsTr("AF Gain")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        onMoved: controller.afGain = value
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
                        text: qsTr("Squelch")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        text: qsTr("Attenuator")
                        Layout.preferredWidth: sideFlick.labelColW
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
                                    function onSFromChanged() { tickCanvas.requestPaint(); }
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
                        text: qsTr("Preamp")
                        Layout.preferredWidth: sideFlick.labelColW
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
                        readonly property bool rxVisible: spec ? (spec.rxVisible ?? false) : false

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

                            Item {
                                Layout.fillWidth: true
                                visible: antCtl.rxVisible && antCtl.options.length > 0
                            } // pushes RX to the right

                            CheckBox {
                                text: qsTr("RX")
                                visible: antCtl.rxVisible
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
                                text: qsTr("Scope enabled")
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
        // --- always-visible right edge handle (drag to open/close) ---

        Rectangle {
            id: sideHandle
            width: 10
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: "transparent"
            z: 10000

            visible: !sidePanel.open
            enabled: !sidePanel.open

            DragHandler {
                enabled: sideHandle.enabled
                onActiveChanged: {
                    if (active) sidePanel.open = true
                    else {
                        sidePanel.open = (sidePanel.slide > sidePanel.width * 0.5)
                        sidePanel.slide = sidePanel.open ? sidePanel.width : 0
                    }
                }
                onTranslationChanged: {
                    var s = -translation.x
                    sidePanel.slide = Math.max(0, Math.min(sidePanel.width, s))
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: sideHandle.enabled
                onClicked: {
                    sidePanel.open = true
                    sidePanel.slide = sidePanel.width
                }
            }
        }


        ToolButton {
            id: drawerButton
            text: qsTr("\u2630")
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            onClicked: {
                sidePanel.open = !sidePanel.open
                sidePanel.slide = sidePanel.open ? sidePanel.width : 0
            }
        }
    }



    // Small dialog for entering edge number (used by toFixed)
    Dialog {
        id: edgeDialog
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)

        background: Rectangle {
            color: root.palette.window
            border.color: root.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: ColumnLayout {

            spacing: 8

            Label {
                text: qsTr("Edge to replace:")
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

        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)

        background: Rectangle {
            color: root.palette.window
            border.color: root.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: qsTr("Start frequency (kHz):")
            }

            SpinBox {
                id: startEdgeFreq
                editable: true
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("End frequency (kHz):")
            }

            SpinBox {
                id: endEdgeFreq
                editable: true
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Invalid span size")
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

    Component.onCompleted: {
        bandPanel.locked = boolReceiverSetting("BandDrawerLocked", false)
        bandPanel.open = boolReceiverSetting("BandDrawerOpen", bandPanel.locked) || bandPanel.locked
        bandPanel.slide = bandPanel.open ? bandPanel.width : 0

        sidePanel.locked = boolReceiverSetting("ControlDrawerLocked", false)
        sidePanel.open = boolReceiverSetting("ControlDrawerOpen", sidePanel.locked) || sidePanel.locked
        sidePanel.slide = sidePanel.open ? sidePanel.width : 0
        root.receiverFullScreen = boolReceiverSetting("DetachedFullScreen", false)

        root.restoringReceiverUiState = false
    }
}
