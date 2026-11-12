import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

// One meter with a primary and a secondary reading, drawn either as a
// vintage face (AnalogMeter) or as two modern bars (Meter).
// Right-click or double-click opens the Primary/Secondary picker.
Item {
    id: panel

    // 0 Modern, 1 Analog, 2 Analog Cream, 3 LCD, 4 LCD Arc
    property int panelStyle: 1
    property int primaryChoice: 1
    property int secondaryChoice: 0
    // Receiver to follow for S+Power; -1 follows the active receiver.
    property int receiverIndex: -1
    property string caption: ""
    // Non-empty: register this panel's meters for polling under this name.
    // Leave empty when the meters are already polled (the meter slot prefs).
    property string consumerId: ""
    property bool pollingActive: visible
    property bool showVfoOption: false
    property bool vfoChecked: false
    property color windowTextColor: "black"  // set by the owner from its palette

    signal primaryChosen(int value)
    signal secondaryChosen(int value)
    signal styleChosen(int value)
    signal menuClosed()
    signal vfoToggled(bool checked)
    signal menuNeedsSize(int width, int height)

    // MainController and its settings go away first at shutdown, while
    // bindings here may still run, so every access is guarded.
    readonly property var opts: MainController.settings ? MainController.settings.options : null
    function opt(key) { return opts ? opts[key] : undefined }
    // Colour properties can't take undefined, which opt() returns at shutdown.
    readonly property color highScaleColor: {
        const v = opt("Color.MeterHighScale")
        return (v === undefined || v === null || v === "") ? "#c83232" : v
    }
    function request(name, type) {
        if (typeof MainController.requestMeter === "function")
            MainController.requestMeter(name, type)
    }

    readonly property var rx: {
        const count = MainController.receiverCount
        if (receiverIndex >= 0)
            return receiverIndex < count ? MainController.receiver(receiverIndex) : null
        for (let i = 0; i < count; ++i) {
            const r = MainController.receiver(i)
            if (r && r.active)
                return r
        }
        return count > 0 ? MainController.receiver(0) : null
    }

    // S+Power (1) follows the receiver's primary meter: S on receive, Po on transmit.
    function typeFor(choice) {
        if (choice === 1)
            return rx ? Number(rx.meterType) : 1
        return choice
    }
    function levelFor(choice) {
        if (choice === 1)
            return rx ? rx.meter : 0
        return typeof MainController.meterLevel === "function" ? MainController.meterLevel(choice) : 0
    }

    readonly property int primaryType: typeFor(primaryChoice)
    readonly property int secondaryType: typeFor(secondaryChoice)
    readonly property double primaryLevel: { MainController.meterLevelsRevision; return levelFor(primaryChoice) }
    readonly property double secondaryLevel: { MainController.meterLevelsRevision; return levelFor(secondaryChoice) }

    //
    // Polling
    //

    readonly property int wantedPrimary: (pollingActive && consumerId !== "") ? primaryChoice : 0
    readonly property int wantedSecondary: (pollingActive && consumerId !== "") ? secondaryChoice : 0
    onWantedPrimaryChanged: if (consumerId !== "") request(consumerId + ".primary", wantedPrimary)
    onWantedSecondaryChanged: if (consumerId !== "") request(consumerId + ".secondary", wantedSecondary)
    Component.onCompleted: {
        if (consumerId === "")
            return
        request(consumerId + ".primary", wantedPrimary)
        request(consumerId + ".secondary", wantedSecondary)
    }
    Component.onDestruction: {
        if (consumerId === "")
            return
        request(consumerId + ".primary", 0)
        request(consumerId + ".secondary", 0)
    }

    //
    // Full-scale marks for the vintage faces
    //

    function extremities(type) {
        MainController.uiSpecs
        return typeof MainController.optionalMeterExtremities === "function"
                ? MainController.optionalMeterExtremities(type) : { valid: false }
    }
    readonly property var powerExt: extremities(4)
    readonly property var currentExt: extremities(8)
    readonly property var voltageExt: extremities(7)
    // Full scale is the rig file's top calibration point.
    readonly property real powerFS: powerExt.valid && powerExt.high > 0 ? powerExt.high : 100
    readonly property real currentFS: currentExt.valid && currentExt.high > 0 ? currentExt.high : 25
    // Vd: rig's top calibration point, nominal (red line) voltage marked,
    // scale starting at three quarters of nominal (10 V for a 13.8 V radio).
    readonly property real vdHigh: voltageExt.valid && voltageExt.high > 0 ? voltageExt.high : 16
    readonly property real vdRed: voltageExt.valid && voltageExt.red > 0 && voltageExt.red < vdHigh ? voltageExt.red : 0
    readonly property real vdLow: vdRed > 0 ? Math.floor(vdRed * 0.75) : Math.floor(vdHigh * 0.625)

    //
    // Vintage face
    //

    AnalogMeter {
        id: vintage
        anchors.fill: parent
        visible: panel.panelStyle > 0
        style: Math.max(0, panel.panelStyle - 1)
        primaryType: panel.primaryType
        primaryValue: panel.primaryLevel
        secondaryType: panel.secondaryType
        secondaryValue: panel.secondaryLevel
        transmitting: MainController.transmitting
        caption: panel.caption
        powerFullScale: panel.powerFS
        currentFullScale: panel.currentFS
        vdLow: panel.vdLow
        vdHigh: panel.vdHigh
        vdRed: panel.vdRed
        dotColors: [
            panel.opt("Color.MeterLevel"),
            panel.opt("Color.MeterPeakLevel"),
            panel.opt("Color.MeterAverage"),
            panel.opt("Color.MeterHighScale"),
            panel.opt("Color.MeterScale"),
            panel.opt("Color.MeterText"),
            panel.opt("Color.MeterLowerLine"),
            panel.opt("Color.MeterLowText")
        ]
    }

    //
    // Modern bars
    //

    function applyModernExtremities(meter, type) {
        const ext = extremities(type)
        if (ext.valid) {
            meter.setMeterExtremities(ext.low, ext.high, ext.red)
            meter.warningText = ""
        } else if (type === 1 || type === 17) {
            meter.setMeterExtremities(-54, 60, 0)
            meter.warningText = ""
        } else if (type === 3) {
            meter.setMeterExtremities(1, 6, 3)
            meter.warningText = ""
        } else {
            meter.clearMeterExtremities()
            meter.warningText = ext.calibrationMissing ? qsTr("No calibration found") : ""
        }
    }

    function settingAsBool(value) {
        return value === true || value === "true" || value === 1 || value === "1"
    }

    function applyAllModernExtremities() {
        applyModernExtremities(modernPrimary, primaryType)
        applyModernExtremities(modernSecondary, secondaryType)
    }
    onPrimaryTypeChanged: applyModernExtremities(modernPrimary, primaryType)
    onSecondaryTypeChanged: applyModernExtremities(modernSecondary, secondaryType)
    Connections {
        target: MainController
        function onUiSpecsChanged() { panel.applyAllModernExtremities() }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: panel.panelStyle === 0
        spacing: 2

        Meter {
            id: modernPrimary
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.primaryType !== 0
            meterType: panel.primaryType
            current: panel.primaryLevel
            drawLabels: true
            fontSize: Math.max(9, Math.min(14, Math.round(height / 4)))
            reverseCompMeter: panel.settingAsBool(panel.opt("Interface.CompMeterReverse"))
            scaleTextColor: panel.windowTextColor
            scaleLineColor: panel.windowTextColor
            scaleHighTextColor: panel.highScaleColor
            scaleHighLineColor: panel.highScaleColor
            Component.onCompleted: panel.applyModernExtremities(modernPrimary, panel.primaryType)
        }
        Meter {
            id: modernSecondary
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.secondaryType !== 0
            meterType: panel.secondaryType
            current: panel.secondaryLevel
            drawLabels: true
            fontSize: Math.max(9, Math.min(14, Math.round(height / 4)))
            reverseCompMeter: panel.settingAsBool(panel.opt("Interface.CompMeterReverse"))
            scaleTextColor: panel.windowTextColor
            scaleLineColor: panel.windowTextColor
            scaleHighTextColor: panel.highScaleColor
            scaleHighLineColor: panel.highScaleColor
            Component.onCompleted: panel.applyModernExtremities(modernSecondary, panel.secondaryType)
        }
        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.primaryChoice === 0 && panel.secondaryChoice === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Right-click to choose meters")
            color: panel.windowTextColor
        }
    }

    //
    // Picker
    //

    MouseArea {
        id: pickerArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                choiceMenu.openAt(pickerArea, mouse.x, mouse.y)
        }
        onDoubleClicked: function(mouse) { choiceMenu.openAt(pickerArea, mouse.x, mouse.y) }
    }

    MeterChoiceMenu {
        id: choiceMenu
        primaryChoice: panel.primaryChoice
        secondaryChoice: panel.secondaryChoice
        styleChoice: panel.panelStyle
        showVfoOption: panel.showVfoOption
        vfoChecked: panel.vfoChecked
        onPrimaryChosen: function(value) { panel.primaryChosen(value) }
        onSecondaryChosen: function(value) { panel.secondaryChosen(value) }
        onStyleChosen: function(value) { panel.styleChosen(value) }
        onClosed: panel.menuClosed()
        onVfoToggled: function(checked) { panel.vfoToggled(checked) }
        onNeedsSize: function(width, height) { panel.menuNeedsSize(width, height) }
    }
}
