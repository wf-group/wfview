import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

// Pop-out meter window: Settings > User Interface > Metering > Pop-out meter.
// Shows the active receiver's meter. On dual-receiver radios a second meter
// for the other VFO can be added beside it (setting, or the right-click menu);
// then the meters show MAIN and SUB.
Window {
    id: popout

    property bool radioConnected: false
    property color windowTextColor: "black"
    property color windowColor: "white"   // Modern style background, from the owner's palette

    // MainController's settings go away first at shutdown; guard every access.
    readonly property var opts: MainController.settings ? MainController.settings.options : null
    function optInt(key, fallback) {
        const v = opts ? opts[key] : undefined
        return (v === undefined || v === null || v === "") ? fallback : Number(v)
    }
    function optBool(key) {
        const v = opts ? opts[key] : undefined
        return v === true || v === "true" || v === 1 || v === "1"
    }
    function setOpt(key, value) {
        if (MainController.settings)
            MainController.settings.setOption(key, value)
    }

    readonly property bool enabledInSettings: optBool("Interface.PopoutMeter")
    // 0 Modern, 1 Analog, 2 Analog Cream, 3 LCD, 4 LCD Arc
    readonly property int meterStyle: optInt("Interface.PopoutMeterStyle", 1)
    // receiverCount only grows past one once rigCaps for a dual-receiver radio
    // have arrived, so the saved setting waits for them.
    readonly property bool dualCapable: MainController.receiverCount > 1
    readonly property bool showSecond: dualCapable && optBool("Interface.PopoutSecondaryVfo")
    readonly property int shownCount: showSecond ? 2 : 1

    // Face aspect is 420 x 200.
    readonly property real faceAspect: 2.1

    title: qsTr("Meter")
    visible: radioConnected && enabledInSettings
    minimumWidth: Math.round(shownCount * 100 * faceAspect)
    minimumHeight: 100
    width: optInt("Interface.PopoutMeterWidth", 0) > 0 ? optInt("Interface.PopoutMeterWidth", 0)
                                                        : Math.round(shownCount * 210 * faceAspect)
    height: optInt("Interface.PopoutMeterHeight", 0) > 0 ? optInt("Interface.PopoutMeterHeight", 0) : 210
    color: meterStyle === 1 ? "black"
         : meterStyle === 2 ? "#cdbe9b"
         : meterStyle >= 3 ? "#e2a046"
         : windowColor
    flags: Qt.Window | Qt.WindowStaysOnTopHint

    onShownCountChanged: width = Math.max(minimumWidth, Math.round(shownCount * height * faceAspect))

    // Closing the window turns the pop-out off.
    onClosing: setOpt("Interface.PopoutMeter", false)

    // Remember the size, without writing settings on every resize step.
    // Size growth for the menu is temporary and not saved.
    onWidthChanged: if (visible && !sizeBeforeMenu) saveSizeTimer.restart()
    onHeightChanged: if (visible && !sizeBeforeMenu) saveSizeTimer.restart()
    Timer {
        id: saveSizeTimer
        interval: 750
        onTriggered: {
            popout.setOpt("Interface.PopoutMeterWidth", popout.width)
            popout.setOpt("Interface.PopoutMeterHeight", popout.height)
        }
    }

    // The menu can't extend past the window, so the window grows while the
    // menu is open and snaps back when it closes.
    property var sizeBeforeMenu: null
    function growFor(w, h) {
        if (!sizeBeforeMenu)
            sizeBeforeMenu = { width: width, height: height }
        width = Math.max(width, w + 8)
        height = Math.max(height, h + 8)
    }
    function restoreAfterMenu() {
        if (!sizeBeforeMenu)
            return
        const s = sizeBeforeMenu
        sizeBeforeMenu = null
        width = Math.max(minimumWidth, s.width)
        height = Math.max(minimumHeight, s.height)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6

        MeterPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            panelStyle: popout.meterStyle
            receiverIndex: popout.showSecond ? 0 : -1
            caption: popout.showSecond ? qsTr("MAIN") : ""
            primaryChoice: popout.optInt("Interface.PopoutMeterPrimary", 1)
            secondaryChoice: popout.optInt("Interface.PopoutMeterSecondary", 0)
            consumerId: "popout0"
            pollingActive: popout.visible
            showVfoOption: popout.dualCapable
            vfoChecked: popout.showSecond
            windowTextColor: popout.windowTextColor
            onPrimaryChosen: function(value) { popout.setOpt("Interface.PopoutMeterPrimary", value) }
            onSecondaryChosen: function(value) { popout.setOpt("Interface.PopoutMeterSecondary", value) }
            onVfoToggled: function(checked) { popout.setOpt("Interface.PopoutSecondaryVfo", checked) }
            onMenuNeedsSize: function(w, h) { popout.growFor(w, h) }
            onMenuClosed: popout.restoreAfterMenu()
            onStyleChosen: function(value) { popout.setOpt("Interface.PopoutMeterStyle", value) }
        }

        MeterPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: popout.showSecond
            panelStyle: popout.meterStyle
            receiverIndex: 1
            caption: qsTr("SUB")
            primaryChoice: popout.optInt("Interface.PopoutMeter2Primary", 1)
            secondaryChoice: popout.optInt("Interface.PopoutMeter2Secondary", 0)
            consumerId: "popout1"
            pollingActive: popout.visible && visible
            showVfoOption: popout.dualCapable
            vfoChecked: popout.showSecond
            windowTextColor: popout.windowTextColor
            onPrimaryChosen: function(value) { popout.setOpt("Interface.PopoutMeter2Primary", value) }
            onSecondaryChosen: function(value) { popout.setOpt("Interface.PopoutMeter2Secondary", value) }
            onVfoToggled: function(checked) { popout.setOpt("Interface.PopoutSecondaryVfo", checked) }
            onMenuNeedsSize: function(w, h) { popout.growFor(w, h) }
            onMenuClosed: popout.restoreAfterMenu()
            onStyleChosen: function(value) { popout.setOpt("Interface.PopoutMeterStyle", value) }
        }
    }
}
