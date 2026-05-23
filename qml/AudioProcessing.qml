import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: win
    title: "Audio Processing"
    width: 720
    height: 900
    minimumWidth: 620
    minimumHeight: 520

    property var controller
    property var txInBins: []
    property var txOutBins: []
    property var rxInBins: []
    property var rxOutBins: []
    property real txInputLevel: 0
    property real txOutputLevel: 0
    property real txGainReduction: 0
    property real rxInputLevel: 0
    property real rxOutputLevel: 0
    property int txBlocks: 0
    property int rxBlocks: 0
    property bool txSpectrumEnabled: false
    property bool rxSpectrumEnabled: false

    function opt(key, fallback) {
        if (!controller || !controller.options || controller.options[key] === undefined)
            return fallback
        return controller.options[key]
    }

    function setOpt(key, value) {
        if (!controller)
            return
        controller.setOption(key, value)
        MainController.applyAudioProcessingSettings()
    }

    function comboIndex(combo, value) {
        var sv = String(value)
        for (var i = 0; i < combo.count; ++i) {
            var item = combo.model.get ? combo.model.get(i) : combo.model[i]
            if (item && String(item.value) === sv)
                return i
        }
        return -1
    }

    component SwitchRow: CheckBox {
        id: switchRow
        property string key: ""
        checked: Boolean(win.opt(key, false))
        onToggled: win.setOpt(key, checked)

        indicator: Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            x: switchRow.leftPadding
            y: switchRow.topPadding + (switchRow.availableHeight - height) / 2
            radius: 2
            color: switchRow.enabled
                   ? (switchRow.checked ? switchRow.palette.highlight : switchRow.palette.base)
                   : switchRow.palette.mid
            border.width: 1
            border.color: switchRow.enabled ? switchRow.palette.text : switchRow.palette.mid

            Label {
                anchors.centerIn: parent
                text: "X"
                visible: switchRow.checked
                color: switchRow.palette.highlightedText
                font.bold: true
                font.pixelSize: 11
            }
        }

        contentItem: Label {
            text: switchRow.text
            font: switchRow.font
            color: switchRow.enabled ? switchRow.palette.text : switchRow.palette.mid
            verticalAlignment: Text.AlignVCenter
            leftPadding: switchRow.indicator.width + switchRow.spacing
        }
    }

    component SliderRow: RowLayout {
        id: row
        property string key: ""
        property string label: ""
        property real from: 0
        property real to: 100
        property real step: 1
        property int decimals: 0
        property string suffix: ""
        property int maxSliderWidth: 0
        spacing: 8
        Layout.preferredHeight: 28
        Label { text: row.label; Layout.preferredWidth: 118; elide: Text.ElideRight }
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.maximumWidth: row.maxSliderWidth > 0 ? row.maxSliderWidth : Number.POSITIVE_INFINITY
            Layout.preferredHeight: 22
            from: row.from
            to: row.to
            stepSize: row.step
            value: Number(win.opt(row.key, row.from))
            onMoved: win.setOpt(row.key, value)
        }
        Label {
            Layout.preferredWidth: 70
            horizontalAlignment: Text.AlignRight
            text: Number(slider.value).toFixed(row.decimals) + row.suffix
        }
    }

    component ComboRow: RowLayout {
        property string key: ""
        property string label: ""
        property var values: []
        spacing: 8
        Label { text: parent.label; Layout.preferredWidth: 118 }
        ComboBox {
            id: combo
            Layout.preferredWidth: 210
            textRole: "text"
            valueRole: "value"
            model: values
            currentIndex: win.comboIndex(combo, win.opt(key, 0))
            onActivated: win.setOpt(key, currentValue)
        }
        Item { Layout.fillWidth: true }
    }

    component VerticalEq: ColumnLayout {
        id: eq
        property string key: ""
        property string label: ""
        property real from: -9
        property real to: 9
        Label {
            text: Number(slider.value).toFixed(1)
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 36
        }
        Slider {
            id: slider
            orientation: Qt.Vertical
            Layout.preferredHeight: 104
            Layout.preferredWidth: 28
            Layout.alignment: Qt.AlignHCenter
            from: eq.from
            to: eq.to
            stepSize: 0.5
            value: Number(win.opt(eq.key, 0))
            onMoved: win.setOpt(eq.key, value)
        }
        Label {
            text: eq.label
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 44
            font.pixelSize: 11
        }
    }

    component LevelMeter: ColumnLayout {
        property string label: ""
        property real value: 0
        spacing: 3
        Label { text: parent.label; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: Math.max(0, Math.min(100, parent.value * 100))
        }
    }

    component SpectrumBox: CollapsibleSection {
        id: box
        property var inputBins: []
        property var outputBins: []
        property string primaryColor: "#3fbf7f"
        property string secondaryColor: "#ff9f43"
        property string enableKey: ""
        property string inhibitKey: ""
        property string fpsKey: ""
        property int blocksProcessed: 0
        property bool processorEnabled: false
        readonly property bool spectrumEnabled: enableKey === "" || Boolean(win.opt(enableKey, false))
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8

            RowLayout {
                Layout.fillWidth: true
                SwitchRow { text: "Enable spectrum"; key: box.enableKey }
                SwitchRow { text: box.inhibitKey.indexOf(".Tx.") >= 0 ? "Inhibit during receive" : "Inhibit during TX"; key: box.inhibitKey }
                Label { text: "FPS" }
                SpinBox {
                    from: 1
                    to: 60
                    value: Number(win.opt(box.fpsKey, 10))
                    onValueModified: win.setOpt(box.fpsKey, value)
                    Layout.preferredWidth: 72
                }
                Item { Layout.fillWidth: true }
            }

            Canvas {
                id: canvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 170
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#11161c"
                    ctx.fillRect(0, 0, width, height)
                    ctx.strokeStyle = "#303a44"
                    ctx.lineWidth = 1
                    for (var i = 1; i < 4; ++i) {
                        var y = height * i / 4
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }
                    if (!box.spectrumEnabled) {
                        ctx.fillStyle = "#9aa4ad"
                        ctx.textAlign = "center"
                        ctx.fillText("Enable spectrum to view audio FFT data", width / 2, height / 2)
                        return
                    }
                    if ((!box.inputBins || box.inputBins.length < 2) && (!box.outputBins || box.outputBins.length < 2)) {
                        ctx.fillStyle = "#9aa4ad"
                        ctx.textAlign = "center"
                        var status = "Waiting for audio spectrum data"
                        status += " | blocks: " + box.blocksProcessed
                        status += " | processor spectrum: " + (box.processorEnabled ? "on" : "off")
                        ctx.fillText(status, width / 2, height / 2)
                        return
                    }
                    draw(box.inputBins, box.primaryColor)
                    draw(box.outputBins, box.secondaryColor)
                    function draw(bins, color) {
                        if (!bins || bins.length < 2) return
                        ctx.strokeStyle = color
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        for (var n = 0; n < bins.length; ++n) {
                            var db = Math.max(-120, Math.min(0, Number(bins[n])))
                            var x = n * width / (bins.length - 1)
                            var y = height - ((db + 120) / 120) * height
                            if (n === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
            }
        }
        onInputBinsChanged: canvas.requestPaint()
        onOutputBinsChanged: canvas.requestPaint()
        onSpectrumEnabledChanged: canvas.requestPaint()
    }

    Connections {
        target: MainController
        function onTxAudioSpectrumChanged(input, output, sampleRate) {
            win.txInBins = input
            win.txOutBins = output
        }
        function onRxAudioSpectrumChanged(input, output, sampleRate) {
            win.rxInBins = input
            win.rxOutBins = output
        }
        function onTxAudioMetersChanged(input, output, gainReduction) {
            win.txInputLevel = input
            win.txOutputLevel = output
            win.txGainReduction = gainReduction / 100.0
        }
        function onRxAudioMetersChanged(input, output) {
            win.rxInputLevel = input
            win.rxOutputLevel = output
        }
        function onAudioProcessingBlocksChanged(txBlocks, rxBlocks) {
            win.txBlocks = txBlocks
            win.rxBlocks = rxBlocks
        }
        function onAudioProcessingSpectrumStateChanged(txEnabled, rxEnabled) {
            win.txSpectrumEnabled = txEnabled
            win.rxSpectrumEnabled = rxEnabled
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "TX Audio Processing" }
            TabButton { text: "RX Audio Processing" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.availableWidth
                    spacing: 8

                    SwitchRow { text: "Master Bypass (disable all DSP)"; key: "AudioProc.Tx.Bypass"; font.bold: true }
                    Label {
                        text: "TX audio blocks processed: " + win.txBlocks
                        color: "#777"
                    }

                    CollapsibleSection {
                        title: "Noise Gate"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            SwitchRow { text: "Enable Noise Gate"; key: "AudioProc.Tx.GateEnabled" }
                            SliderRow { label: "Threshold"; key: "AudioProc.Tx.GateThreshold"; from: -70; to: 0; step: 1; suffix: " dB" }
                            SliderRow { label: "Attack"; key: "AudioProc.Tx.GateAttack"; from: 1; to: 500; step: 1; suffix: " ms" }
                            SliderRow { label: "Hold"; key: "AudioProc.Tx.GateHold"; from: 2; to: 1000; step: 2; suffix: " ms" }
                            SliderRow { label: "Decay"; key: "AudioProc.Tx.GateDecay"; from: 2; to: 1000; step: 2; suffix: " ms" }
                            SliderRow { label: "Range"; key: "AudioProc.Tx.GateRange"; from: -90; to: 0; step: 1; suffix: " dB" }
                            SliderRow { label: "LF cutoff"; key: "AudioProc.Tx.GateLfCutoff"; from: 20; to: 500; step: 5; suffix: " Hz" }
                            SliderRow { label: "HF cutoff"; key: "AudioProc.Tx.GateHfCutoff"; from: 200; to: 4000; step: 10; suffix: " Hz" }
                        }
                    }

                    CollapsibleSection {
                        title: "Plugin Order"
                        Layout.fillWidth: true
                        ComboRow {
                            key: "AudioProc.Tx.EqFirst"
                            label: "Order"
                            values: [{ text: "EQ -> Compressor", value: true }, { text: "Compressor -> EQ", value: false }]
                        }
                    }

                    CollapsibleSection {
                        title: "Gain"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            SliderRow { label: "Input gain"; key: "AudioProc.Tx.InputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB" }
                            SliderRow { label: "Output gain"; key: "AudioProc.Tx.OutputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB" }
                        }
                    }

                    CollapsibleSection {
                        title: "Compressor"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            SwitchRow { text: "Enable Compressor"; key: "AudioProc.Tx.CompEnabled" }
                            SliderRow { label: "Peak limit"; key: "AudioProc.Tx.CompPeakLimit"; from: -30; to: 0; step: 0.5; decimals: 1; suffix: " dB" }
                            SliderRow { label: "Release"; key: "AudioProc.Tx.CompRelease"; from: 0.01; to: 1; step: 0.01; decimals: 2; suffix: " s" }
                            SliderRow { label: "Fast ratio"; key: "AudioProc.Tx.CompFastRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                            SliderRow { label: "Slow ratio"; key: "AudioProc.Tx.CompSlowRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                        }
                    }

                    CollapsibleSection {
                        title: "Equalizer"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                SwitchRow { text: "Enable EQ"; key: "AudioProc.Tx.EqEnabled" }
                                Button {
                                    text: "Clear"
                                    onClicked: {
                                        for (var i = 0; i < 15; ++i)
                                            win.setOpt("AudioProc.Tx.EqBand" + i, 0)
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Repeater {
                                    model: ["25", "40", "63", "100", "160", "250", "400", "630", "1k", "1.6k", "2.5k", "4k", "6.3k", "10k", "16k"]
                                    VerticalEq { key: "AudioProc.Tx.EqBand" + index; label: modelData }
                                }
                            }
                        }
                    }

                    SpectrumBox {
                        title: "TX Spectrum"
                        enableKey: "AudioProc.Tx.SpectrumEnabled"
                        inhibitKey: "AudioProc.Tx.SpecInhibitDuringRx"
                        fpsKey: "AudioProc.Tx.SpectrumFPS"
                        blocksProcessed: win.txBlocks
                        processorEnabled: win.txSpectrumEnabled
                        inputBins: win.txInBins
                        outputBins: win.txOutBins
                    }

                    CollapsibleSection {
                        title: "Self-Monitor"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                SwitchRow { text: "Enable self-monitoring"; key: "AudioProc.Tx.SidetoneEnabled" }
                                SwitchRow { text: "Mute RX Audio"; key: "AudioProc.Tx.MuteRx" }
                            }
                            SliderRow { label: "Level"; key: "AudioProc.Tx.SidetoneLevel"; from: 0; to: 1; step: 0.01; decimals: 2 }
                        }
                    }

                    CollapsibleSection {
                        title: "Meters"
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            LevelMeter { label: "Input"; value: win.txInputLevel }
                            LevelMeter { label: "Output"; value: win.txOutputLevel }
                            LevelMeter { label: "Gain Reduction"; value: win.txGainReduction }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.availableWidth
                    spacing: 8

                    RowLayout {
                        SwitchRow { text: "Master Bypass"; key: "AudioProc.Rx.Bypass"; font.bold: true }
                        ComboRow {
                            key: "AudioProc.Rx.ChannelSelect"
                            label: "Channel"
                            values: [{ text: "Auto", value: 0 }, { text: "Left", value: 1 }, { text: "Right", value: 2 }, { text: "Mono sum", value: 3 }]
                        }
                    }
                    Label {
                        text: "RX audio blocks processed: " + win.rxBlocks
                        color: "#777"
                    }

                    CollapsibleSection {
                        title: "Noise Reduction Algorithm"
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            RadioButton { text: "None"; checked: Number(win.opt("AudioProc.Rx.NrMode", 0)) === 0; onClicked: win.setOpt("AudioProc.Rx.NrMode", 0) }
                            RadioButton { text: "Speex"; checked: Number(win.opt("AudioProc.Rx.NrMode", 0)) === 1; onClicked: win.setOpt("AudioProc.Rx.NrMode", 1) }
                            RadioButton { text: "ANR"; checked: Number(win.opt("AudioProc.Rx.NrMode", 0)) === 2; onClicked: win.setOpt("AudioProc.Rx.NrMode", 2) }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    CollapsibleSection {
                        title: "Speex DSP Noise Reduction"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            SliderRow { label: "Suppression"; key: "AudioProc.Rx.SpeexSuppression"; from: -70; to: -1; step: 1; suffix: " dB"; maxSliderWidth: 260 }
                            SliderRow { label: "Bark bands"; key: "AudioProc.Rx.SpeexBandsPreset"; from: 0; to: 8; step: 1; maxSliderWidth: 260 }
                            ComboRow { label: "Frame size"; key: "AudioProc.Rx.SpeexFrameMs"; values: [{ text: "10 ms frames", value: 10 }, { text: "20 ms frames", value: 20 }] }
                            SwitchRow { text: "Enable automatic gain control (AGC)"; key: "AudioProc.Rx.SpeexAgc" }
                            SliderRow { label: "AGC level"; key: "AudioProc.Rx.SpeexAgcLevel"; from: 500; to: 32000; step: 100; maxSliderWidth: 260 }
                            SliderRow { label: "AGC max gain"; key: "AudioProc.Rx.SpeexAgcMaxGain"; from: 0; to: 60; step: 1; suffix: " dB"; maxSliderWidth: 260 }
                            SwitchRow { text: "Enable voice activity detection (VAD)"; key: "AudioProc.Rx.SpeexVad" }
                            SliderRow { label: "VAD start"; key: "AudioProc.Rx.SpeexVadProbStart"; from: 0; to: 100; step: 1; suffix: " %"; maxSliderWidth: 260 }
                            SliderRow { label: "VAD continue"; key: "AudioProc.Rx.SpeexVadProbCont"; from: 0; to: 100; step: 1; suffix: " %"; maxSliderWidth: 260 }
                            SliderRow { label: "SNR decay"; key: "AudioProc.Rx.SpeexSnrDecay"; from: 0; to: 0.95; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                            SliderRow { label: "Noise update"; key: "AudioProc.Rx.SpeexNoiseUpdateRate"; from: 0.01; to: 0.5; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                            SliderRow { label: "Prior base"; key: "AudioProc.Rx.SpeexPriorBase"; from: 0.05; to: 0.5; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                        }
                    }

                    CollapsibleSection {
                        title: "ANR Noise Reduction"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            SliderRow { label: "Noise reduction"; key: "AudioProc.Rx.AnrNoiseReductionDb"; from: 0; to: 48; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 260 }
                            SliderRow { label: "Sensitivity"; key: "AudioProc.Rx.AnrSensitivity"; from: 0; to: 10; step: 0.1; decimals: 1; maxSliderWidth: 260 }
                            SliderRow { label: "Smoothing"; key: "AudioProc.Rx.AnrFreqSmoothing"; from: 0; to: 6; step: 1; maxSliderWidth: 260 }
                            RowLayout {
                                Button { text: "Collect Noise Sample"; onClicked: MainController.startAnrNoiseProfile() }
                                Button { text: "Stop Collecting"; onClicked: MainController.stopAnrNoiseProfile() }
                                Label { text: "Noise profile is stored per mode"; color: "#777" }
                            }
                        }
                    }

                    CollapsibleSection {
                        title: "RX Equalizer"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                SwitchRow { text: "Enable EQ"; key: "AudioProc.Rx.EqEnabled" }
                                Button {
                                    text: "Clear"
                                    onClicked: {
                                        for (var i = 0; i < 4; ++i)
                                            win.setOpt("AudioProc.Rx.EqGain" + i, 0)
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Repeater {
                                    model: ["Low", "Mid 1", "Mid 2", "High"]
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label { text: modelData; font.bold: true }
                                        SliderRow { label: "Gain"; key: "AudioProc.Rx.EqGain" + index; from: -9; to: 9; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 140 }
                                        SliderRow { label: "Freq"; key: "AudioProc.Rx.EqFreq" + index; from: 20; to: 8000; step: 10; suffix: " Hz"; maxSliderWidth: 140 }
                                        SliderRow { label: "Q"; key: "AudioProc.Rx.EqQ" + index; from: 0.1; to: 10; step: 0.1; decimals: 1; maxSliderWidth: 140 }
                                    }
                                }
                            }
                        }
                    }

                    CollapsibleSection {
                        title: "Output Gain"
                        Layout.fillWidth: true
                        SliderRow { label: "Output gain"; key: "AudioProc.Rx.OutputGainDB"; from: -6; to: 20; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 260 }
                    }

                    SpectrumBox {
                        title: "RX Spectrum"
                        enableKey: "AudioProc.Rx.SpectrumEnabled"
                        inhibitKey: "AudioProc.Rx.SpecInhibitDuringTx"
                        fpsKey: "AudioProc.Rx.SpectrumFPS"
                        blocksProcessed: win.rxBlocks
                        processorEnabled: win.rxSpectrumEnabled
                        inputBins: win.rxInBins
                        outputBins: win.rxOutBins
                    }

                    CollapsibleSection {
                        title: "Meters"
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            LevelMeter { label: "Input"; value: win.rxInputLevel }
                            LevelMeter { label: "Output"; value: win.rxOutputLevel }
                        }
                    }
                }
            }
        }
    }
}
