import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: repeaterWindow
    title: qsTr("Repeater / Split - wfview")
    visible: true
    width: 560
    height: content.implicitHeight + 24
    minimumWidth: 460
    minimumHeight: 360
    color: palette.window
    transientParent: null

    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true
    readonly property var rptState: MainController.repeaterState || ({})
    readonly property var caps: rptState.capabilities || ({})
    property var toneModel: []
    property var dtcsModel: []
    property bool forceClose: false

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

        disabled {
            window: _sysTheme ? undefined : MainController.settings.options["Color.Window"]
            windowText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            base: _sysTheme ? undefined : MainController.settings.options["Color.Base"]
            alternateBase: _sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
            text: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            button: _sysTheme ? undefined : MainController.settings.options["Color.Button"]
            buttonText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            brightText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            highlight: _sysTheme ? undefined : MainController.settings.options["Color.Dark"]
            highlightedText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            mid: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            dark: _sysTheme ? undefined : MainController.settings.options["Color.Dark"]
            light: _sysTheme ? undefined : MainController.settings.options["Color.Light"]
            placeholderText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
        }
    }

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
        }
    }

    function indexFor(model, value) {
        for (var i = 0; i < model.length; ++i) {
            if (Number(model[i].value) === Number(value))
                return i
        }
        return model.length > 0 ? 0 : -1
    }

    function refreshModels() {
        toneModel = MainController.repeaterToneOptions()
        dtcsModel = MainController.repeaterDtcsOptions()
    }

    onClosing: function(close) {
        if (forceClose)
            return
        close.accepted = false
        visible = false
    }

    onVisibleChanged: {
        if (visible) {
            MainController.updateApplicationPalette()
            refreshModels()
            MainController.refreshRepeaterState()
            applyDisabledPalette()
        }
    }

    Component.onCompleted: {
        applyDisabledPalette()
        refreshModels()
        MainController.refreshRepeaterState()
    }

    Connections {
        target: MainController

        function onRepeaterModelsChanged() {
            repeaterWindow.refreshModels()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            id: content
            width: repeaterWindow.width
            spacing: 8

            Label {
                visible: rptState.warnNotFm === true
                Layout.fillWidth: true
                Layout.margins: 10
                text: qsTr("Repeater controls usually require FM mode.")
                color: palette.highlight
                wrapMode: Text.WordWrap
            }

            GroupBox {
                title: qsTr("Duplex")
                visible: caps.duplex === true || caps.repeaterDuplex === true || caps.quickSplit === true || caps.radioOffset === true
                Layout.fillWidth: true
                Layout.margins: 8

                GridLayout {
                    columns: 4
                    rowSpacing: 6
                    columnSpacing: 8
                    anchors.fill: parent

                    Button {
                        text: qsTr("Off")
                        checkable: true
                        checked: Number(rptState.duplexMode) === Number(rptState.dmSplitOff)
                        visible: caps.duplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmSplitOff))
                    }
                    Button {
                        text: qsTr("Split")
                        checkable: true
                        checked: Number(rptState.duplexMode) === Number(rptState.dmSplitOn)
                        visible: caps.duplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmSplitOn))
                    }
                    Button {
                        text: qsTr("DUP-")
                        checkable: true
                        checked: Number(rptState.duplexMode) === Number(rptState.dmDupMinus)
                        visible: caps.repeaterDuplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmDupMinus))
                    }
                    Button {
                        text: qsTr("DUP+")
                        checkable: true
                        checked: Number(rptState.duplexMode) === Number(rptState.dmDupPlus)
                        visible: caps.repeaterDuplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmDupPlus))
                    }
                    Button {
                        text: qsTr("Simplex")
                        visible: caps.repeaterDuplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmSimplex))
                    }
                    Button {
                        text: qsTr("Auto")
                        visible: caps.repeaterDuplex === true
                        onClicked: MainController.setRepeaterDuplex(Number(rptState.dmDupAutoOn))
                    }
                    CheckBox {
                        text: qsTr("Quick split")
                        checked: rptState.quickSplit === true
                        visible: caps.quickSplit === true
                        onClicked: MainController.setRepeaterQuickSplit(checked)
                    }
                    Item { Layout.fillWidth: true }

                    Label {
                        text: qsTr("Radio offset MHz")
                        visible: caps.radioOffset === true
                    }
                    TextField {
                        id: radioOffsetEdit
                        text: rptState.offsetMHz || ""
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        selectByMouse: true
                        visible: caps.radioOffset === true
                        Layout.fillWidth: true
                        onAccepted: MainController.setRepeaterOffsetMHz(text)
                    }
                    Button {
                        text: qsTr("Set")
                        visible: caps.radioOffset === true
                        onClicked: MainController.setRepeaterOffsetMHz(radioOffsetEdit.text)
                    }
                    Item { visible: caps.radioOffset === true; Layout.fillWidth: true }
                }
            }

            GroupBox {
                title: qsTr("Split Frequency")
                visible: caps.duplex === true
                Layout.fillWidth: true
                Layout.margins: 8

                GridLayout {
                    columns: 5
                    rowSpacing: 6
                    columnSpacing: 8
                    anchors.fill: parent

                    Label { text: qsTr("RX MHz") }
                    Label {
                        text: rptState.mainFrequencyMHz || qsTr("waiting")
                        Layout.fillWidth: true
                    }
                    CheckBox {
                        id: autoEnableSplit
                        text: qsTr("Enable split")
                        checked: true
                    }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("Offset kHz") }
                    TextField {
                        id: splitOffsetEdit
                        text: "600"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        selectByMouse: true
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("+")
                        onClicked: MainController.setRepeaterSplitFromOffsetKHz(splitOffsetEdit.text, true, autoEnableSplit.checked)
                    }
                    Button {
                        text: qsTr("-")
                        onClicked: MainController.setRepeaterSplitFromOffsetKHz(splitOffsetEdit.text, false, autoEnableSplit.checked)
                    }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("TX MHz") }
                    TextField {
                        id: splitTxEdit
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        selectByMouse: true
                        Layout.fillWidth: true
                        onAccepted: MainController.setRepeaterSplitTxFrequencyMHz(text, autoEnableSplit.checked)
                    }
                    Button {
                        text: qsTr("Set TX")
                        onClicked: MainController.setRepeaterSplitTxFrequencyMHz(splitTxEdit.text, autoEnableSplit.checked)
                    }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }
                }
            }

            GroupBox {
                title: qsTr("Tone")
                visible: caps.toneMode === true || caps.tone === true || caps.tsql === true || caps.dtcs === true
                Layout.fillWidth: true
                Layout.margins: 8

                GridLayout {
                    columns: 4
                    rowSpacing: 6
                    columnSpacing: 8
                    anchors.fill: parent

                    Button {
                        text: qsTr("None")
                        checkable: true
                        checked: Number(rptState.toneMode) === Number(rptState.ratrNN)
                        visible: caps.toneMode === true
                        onClicked: MainController.setRepeaterToneMode(Number(rptState.ratrNN), setSubTone.checked)
                    }
                    Button {
                        text: qsTr("Tone")
                        checkable: true
                        checked: Number(rptState.toneMode) === Number(rptState.ratrTN)
                        visible: caps.tone === true
                        onClicked: {
                            MainController.setRepeaterToneMode(Number(rptState.ratrTN), setSubTone.checked)
                            if (toneCombo.currentIndex >= 0)
                                MainController.setRepeaterToneFrequency(toneCombo.currentValue, setSubTone.checked)
                        }
                    }
                    Button {
                        text: qsTr("TSQL")
                        checkable: true
                        checked: Number(rptState.toneMode) === Number(rptState.ratrTT)
                        visible: caps.tsql === true
                        onClicked: {
                            MainController.setRepeaterToneMode(Number(rptState.ratrTT), setSubTone.checked)
                            if (tsqlCombo.currentIndex >= 0)
                                MainController.setRepeaterTsqlFrequency(tsqlCombo.currentValue, setSubTone.checked)
                        }
                    }
                    Button {
                        text: qsTr("DCS")
                        checkable: true
                        checked: Number(rptState.toneMode) === Number(rptState.ratrDD)
                        visible: caps.dtcs === true
                        onClicked: {
                            MainController.setRepeaterToneMode(Number(rptState.ratrDD), setSubTone.checked)
                            if (dtcsCombo.currentIndex >= 0)
                                MainController.setRepeaterDtcsCode(dtcsCombo.currentValue, dtcsTxInvert.checked, dtcsRxInvert.checked)
                        }
                    }

                    Label { text: qsTr("Tone"); visible: caps.tone === true }
                    ComboBox {
                        id: toneCombo
                        model: repeaterWindow.toneModel
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: repeaterWindow.indexFor(model, rptState.tone)
                        visible: caps.tone === true
                        Layout.fillWidth: true
                        onActivated: MainController.setRepeaterToneFrequency(currentValue, setSubTone.checked)
                    }
                    Label { text: qsTr("TSQL"); visible: caps.tsql === true }
                    ComboBox {
                        id: tsqlCombo
                        model: repeaterWindow.toneModel
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: repeaterWindow.indexFor(model, rptState.tsql)
                        visible: caps.tsql === true
                        Layout.fillWidth: true
                        onActivated: MainController.setRepeaterTsqlFrequency(currentValue, setSubTone.checked)
                    }

                    Label { text: qsTr("DCS"); visible: caps.dtcs === true }
                    ComboBox {
                        id: dtcsCombo
                        model: repeaterWindow.dtcsModel
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: repeaterWindow.indexFor(model, rptState.dtcs)
                        visible: caps.dtcs === true
                        Layout.fillWidth: true
                        onActivated: MainController.setRepeaterDtcsCode(currentValue, dtcsTxInvert.checked, dtcsRxInvert.checked)
                    }
                    CheckBox {
                        id: dtcsTxInvert
                        text: qsTr("TX inv")
                        checked: rptState.dtcsTxInvert === true
                        visible: caps.dtcs === true
                        onClicked: MainController.setRepeaterDtcsCode(dtcsCombo.currentValue, checked, dtcsRxInvert.checked)
                    }
                    CheckBox {
                        id: dtcsRxInvert
                        text: qsTr("RX inv")
                        checked: rptState.dtcsRxInvert === true
                        visible: caps.dtcs === true
                        onClicked: MainController.setRepeaterDtcsCode(dtcsCombo.currentValue, dtcsTxInvert.checked, checked)
                    }

                    CheckBox {
                        id: setSubTone
                        text: qsTr("Also set secondary VFO")
                        visible: caps.secondaryVfo === true
                        Layout.columnSpan: 4
                    }
                }
            }

        }
    }
}
