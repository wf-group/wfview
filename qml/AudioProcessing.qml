import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: win
    transientParent: null
    title: mode === "rx" ? qsTr("RX Audio Processing") : qsTr("TX Audio Processing")
    color: palette.window
    width: mode === "rx" ? 860 : 635
    height: mode === "rx" ? 660 : 980
    minimumWidth: mode === "rx" ? 760 : 620
    minimumHeight: 520

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

    property string mode: "tx"
    readonly property bool txMode: mode === "tx"
    readonly property bool rxMode: mode === "rx"
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
    // Latest TX processing sample rate, reported via onTxAudioSpectrumChanged.
    // Used to hide Multiband EQ bands that sit at or above the Nyquist limit.
    property real txSampleRate: 48000

    // Multiband EQ band centre frequencies — must stay in sync with the
    // MbeqProcessor::bandFreqs table in src/audio/plugins/mbeq.h. The index of
    // each entry is the backend EqBand index (AudioProc.Tx.EqBandN).
    readonly property var eqBands: [
        { freq: 50,   label: "50" },
        { freq: 100,  label: "100" },
        { freq: 200,  label: "200" },
        { freq: 300,  label: "300" },
        { freq: 400,  label: "400" },
        { freq: 600,  label: "600" },
        { freq: 800,  label: "800" },
        { freq: 1000, label: "1.0k" },
        { freq: 1200, label: "1.2k" },
        { freq: 1600, label: "1.6k" },
        { freq: 2000, label: "2.0k" },
        { freq: 2500, label: "2.5k" },
        { freq: 3200, label: "3.2k" },
        { freq: 5000, label: "5.0k" },
        { freq: 8000, label: "8.0k" }
    ]

    // Bands below the Nyquist frequency, each carrying its original backend
    // index so the correct AudioProc.Tx.EqBandN setting is driven.
    function visibleEqBands() {
        var nyquist = win.txSampleRate / 2
        var out = []
        for (var i = 0; i < win.eqBands.length; ++i)
            if (win.eqBands[i].freq < nyquist)
                out.push({ idx: i, label: win.eqBands[i].label })
        return out
    }

    function opt(key, fallback) {
        if (!controller || !controller.options || controller.options[key] === undefined)
            return fallback
        return controller.options[key]
    }

    function boolValue(value, fallback) {
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
        return fallback === undefined ? false : boolValue(fallback, false)
    }

    function optBool(key, fallback) {
        return boolValue(opt(key, fallback), fallback)
    }

    function setOpt(key, value) {
        if (!controller)
            return
        controller.setOption(key, value)
        MainController.applyAudioProcessingSettings()
    }

    onVisibleChanged: {
        if (visible)
            MainController.updateApplicationPalette()
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

    function canvasColor(c, alpha) {
        if (c === undefined || c === null)
            return "rgba(255,255,255," + alpha + ")"
        return "rgba("
                + Math.round(c.r * 255) + ","
                + Math.round(c.g * 255) + ","
                + Math.round(c.b * 255) + ","
                + (alpha === undefined ? c.a : alpha) + ")"
    }

    component SwitchRow: CheckBox {
        id: switchRow
        property string key: ""
        // Keyed switches reflect/persist a setting. Keyless switches are plain
        // runtime checkboxes (default unchecked) — the caller handles onToggled.
        Binding on checked {
            when: switchRow.key !== ""
            value: win.optBool(switchRow.key, false)
        }
        onToggled: if (switchRow.key !== "") win.setOpt(switchRow.key, checked)

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
                text: qsTr("X")
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
        property real displayScale: 1
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
            text: Number(slider.value * row.displayScale).toFixed(row.decimals) + row.suffix
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
        readonly property bool spectrumEnabled: enableKey === "" || win.optBool(enableKey, false)
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8

            RowLayout {
                Layout.fillWidth: true
                SwitchRow { text: qsTr("Enable spectrum"); key: box.enableKey }
                SwitchRow { text: box.inhibitKey.indexOf(".Tx.") >= 0 ? qsTr("Inhibit during receive") : qsTr("Inhibit during TX"); key: box.inhibitKey }
                Label { text: qsTr("FPS") }
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
                    var minDb = -90
                    var maxDb = 0
                    var left = 42
                    var right = 8
                    var top = 8
                    var bottom = 24
                    var plotX = left
                    var plotY = top
                    var plotW = Math.max(1, width - left - right)
                    var plotH = Math.max(1, height - top - bottom)
                    var bgColor = win.canvasColor(win.palette.base, 1)
                    var gridColor = win.canvasColor(win.palette.mid, 1)
                    var textColor = win.canvasColor(win.palette.text, 1)
                    var subtleTextColor = win.canvasColor(win.palette.text, 0.75)

                    ctx.fillStyle = bgColor
                    ctx.fillRect(0, 0, width, height)

                    drawGrid()

                    function dbToY(db) {
                        return plotY + plotH - ((db - minDb) / (maxDb - minDb)) * plotH
                    }

                    function freqToX(freq) {
                        var logMin = Math.log(50) / Math.LN10
                        var logMax = Math.log(8000) / Math.LN10
                        var logFreq = Math.log(Math.max(50, freq)) / Math.LN10
                        return plotX + ((logFreq - logMin) / (logMax - logMin)) * plotW
                    }

                    function freqLabel(freq) {
                        if (freq >= 1000)
                            return (freq / 1000).toFixed(freq % 1000 === 0 ? 0 : 1) + "k"
                        return String(freq)
                    }

                    function drawGrid() {
                        ctx.save()
                        ctx.font = "10px sans-serif"
                        ctx.textBaseline = "middle"
                        ctx.lineWidth = 1
                        ctx.strokeStyle = gridColor
                        ctx.fillStyle = subtleTextColor

                        var dbStep = 6
                        while (plotH * dbStep / (maxDb - minDb) < 14 && dbStep < (maxDb - minDb))
                            dbStep += 6

                        ctx.setLineDash([3, 3])
                        for (var db = minDb; db <= maxDb; db += dbStep) {
                            var y = dbToY(db)
                            ctx.beginPath()
                            ctx.moveTo(plotX, y)
                            ctx.lineTo(plotX + plotW, y)
                            ctx.stroke()
                            ctx.textAlign = "right"
                            ctx.fillText(String(db), plotX - 5, y)
                        }

                        var freqs = [50, 100, 200, 400, 800, 1600, 3200, 6400, 8000]
                        ctx.textAlign = "center"
                        for (var f = 0; f < freqs.length; ++f) {
                            var x = freqToX(freqs[f])
                            ctx.beginPath()
                            ctx.moveTo(x, plotY)
                            ctx.lineTo(x, plotY + plotH)
                            ctx.stroke()
                            ctx.fillText(freqLabel(freqs[f]), x, plotY + plotH + 13)
                        }
                        ctx.setLineDash([])

                        ctx.strokeStyle = textColor
                        ctx.beginPath()
                        ctx.rect(plotX, plotY, plotW, plotH)
                        ctx.stroke()

                        ctx.textAlign = "left"
                        ctx.textBaseline = "top"
                        ctx.fillText("dBFS", 4, plotY)
                        ctx.restore()
                    }

                    if (!box.spectrumEnabled) {
                        ctx.fillStyle = textColor
                        ctx.textAlign = "center"
                        ctx.fillText(qsTr("Enable spectrum to view audio FFT data"), plotX + plotW / 2, plotY + plotH / 2)
                        return
                    }
                    if ((!box.inputBins || box.inputBins.length < 2) && (!box.outputBins || box.outputBins.length < 2)) {
                        ctx.fillStyle = textColor
                        ctx.textAlign = "center"
                        var status = qsTr("Waiting for audio spectrum data")
                        status += qsTr(" | blocks: ") + box.blocksProcessed
                        status += qsTr(" | processor spectrum: ") + (box.processorEnabled ? qsTr("on") : qsTr("off"))
                        ctx.fillText(status, plotX + plotW / 2, plotY + plotH / 2)
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
                            var db = Math.max(minDb, Math.min(maxDb, Number(bins[n])))
                            var x = plotX + n * plotW / (bins.length - 1)
                            var y = dbToY(db)
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
            if (sampleRate > 0)
                win.txSampleRate = sampleRate
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

    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 7
        clip: true
        contentWidth: availableWidth

        Loader {
            width: scrollView.availableWidth
            sourceComponent: win.txMode ? txPage : rxPage
        }
    }

    Component {
        id: txPage
        ColumnLayout {
            width: parent ? parent.width : 620
            spacing: 16

            SwitchRow { text: qsTr("Master Bypass (disable all DSP)"); key: "AudioProc.Tx.Bypass"; font.bold: true }

            CollapsibleSection {
                title: qsTr("Noise Gate (pre-gain)")
                expanded: false
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        SwitchRow { text: qsTr("Enable Noise Gate"); key: "AudioProc.Tx.GateEnabled" }
                        Item { Layout.fillWidth: true }
                    }
                    SliderRow { label: qsTr("Threshold"); key: "AudioProc.Tx.GateThreshold"; from: -70; to: 0; step: 1; suffix: " dB" }
                    SliderRow { label: qsTr("Attack"); key: "AudioProc.Tx.GateAttack"; from: 1; to: 500; step: 1; suffix: " ms" }
                    SliderRow { label: qsTr("Hold"); key: "AudioProc.Tx.GateHold"; from: 2; to: 1000; step: 2; suffix: " ms" }
                    SliderRow { label: qsTr("Decay"); key: "AudioProc.Tx.GateDecay"; from: 2; to: 1000; step: 2; suffix: " ms" }
                    SliderRow { label: qsTr("Range"); key: "AudioProc.Tx.GateRange"; from: -90; to: 0; step: 1; suffix: " dB" }
                    SliderRow { label: qsTr("LF cutoff"); key: "AudioProc.Tx.GateLfCutoff"; from: 20; to: 500; step: 5; suffix: " Hz" }
                    SliderRow { label: qsTr("HF cutoff"); key: "AudioProc.Tx.GateHfCutoff"; from: 200; to: 4000; step: 10; suffix: " Hz" }
                }
            }

            ComboRow {
                key: "AudioProc.Tx.EqFirst"
                label: qsTr("Plugin order")
                values: [{ text: qsTr("EQ -> Compressor"), value: true }, { text: qsTr("Compressor -> EQ"), value: false }]
            }

            GroupBox {
                title: qsTr("Gain")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    SliderRow { label: qsTr("Input gain"); key: "AudioProc.Tx.InputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 86 }
                    SliderRow { label: qsTr("Output gain"); key: "AudioProc.Tx.OutputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 86 }
                }
            }

            CollapsibleSection {
                title: qsTr("Multiband EQ")
                expanded: false
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        SwitchRow { text: qsTr("Enable EQ"); key: "AudioProc.Tx.EqEnabled" }
                        Button {
                            text: qsTr("Clear")
                            onClicked: {
                                for (var i = 0; i < win.eqBands.length; ++i)
                                    win.setOpt("AudioProc.Tx.EqBand" + i, 0)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: win.visibleEqBands()
                            VerticalEq { key: "AudioProc.Tx.EqBand" + modelData.idx; label: modelData.label }
                        }
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Dyson Compressor")
                ColumnLayout {
                    anchors.fill: parent
                    SwitchRow { text: qsTr("Enable Compressor"); key: "AudioProc.Tx.CompEnabled" }
                    SliderRow { label: qsTr("Peak limit"); key: "AudioProc.Tx.CompPeakLimit"; from: -30; to: 0; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 86 }
                    SliderRow { label: qsTr("Release time"); key: "AudioProc.Tx.CompRelease"; from: 0.01; to: 1; step: 0.01; decimals: 2; suffix: " s"; maxSliderWidth: 86 }
                    SliderRow { label: qsTr("Fast ratio"); key: "AudioProc.Tx.CompFastRatio"; from: 0; to: 1; step: 0.01; decimals: 2; maxSliderWidth: 86 }
                    SliderRow { label: qsTr("Slow ratio"); key: "AudioProc.Tx.CompSlowRatio"; from: 0; to: 1; step: 0.01; decimals: 2; maxSliderWidth: 86 }
                }
            }

            SpectrumBox {
                title: qsTr("TX Spectrum")
                enableKey: "AudioProc.Tx.SpectrumEnabled"
                inhibitKey: "AudioProc.Tx.SpecInhibitDuringRx"
                fpsKey: "AudioProc.Tx.SpectrumFPS"
                blocksProcessed: win.txBlocks
                processorEnabled: win.txSpectrumEnabled
                inputBins: win.txInBins
                outputBins: win.txOutBins
            }

            GroupBox {
                title: qsTr("Self-Monitor")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        SwitchRow { text: qsTr("Enable self-monitoring"); key: "AudioProc.Tx.SidetoneEnabled" }
                        SwitchRow {
                            id: muteRxSwitch
                            text: qsTr("Mute RX Audio")
                            enabled: win.optBool("AudioProc.Tx.SidetoneEnabled", false)
                            onToggled: MainController.setRxMuted(checked)
                            // Clear the mute if self-monitoring is turned off, so RX
                            // audio can't be left muted by a now-disabled control.
                            onEnabledChanged: if (!enabled && checked) { checked = false; MainController.setRxMuted(false) }
                        }
                    }
                    SliderRow { label: qsTr("Monitor level"); key: "AudioProc.Tx.SidetoneLevel"; from: 0; to: 1; step: 0.01; decimals: 0; suffix: "%"; maxSliderWidth: 86; displayScale: 100 }
                }
            }

            GroupBox {
                title: qsTr("Meters")
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    LevelMeter { label: qsTr("Input"); value: win.txInputLevel }
                    LevelMeter { label: qsTr("Output"); value: win.txOutputLevel }
                    LevelMeter { label: qsTr("Gain Reduction"); value: win.txGainReduction }
                }
            }
        }
    }

    Component {
        id: rxPage
        ColumnLayout {
            width: parent ? parent.width : 840
            spacing: 14
            readonly property int nrMode: Number(win.opt("AudioProc.Rx.NrMode", 0))

            RowLayout {
                Layout.fillWidth: true
                SwitchRow { text: qsTr("Master Bypass (pass audio unchanged)"); key: "AudioProc.Rx.Bypass"; font.bold: true }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: rxChannelCombo
                    Layout.preferredWidth: 420
                    textRole: "text"
                    valueRole: "value"
                    model: [{ text: qsTr("Auto"), value: 0 }, { text: qsTr("Left"), value: 1 }, { text: qsTr("Right"), value: 2 }, { text: qsTr("Mono"), value: 3 }]
                    currentIndex: win.comboIndex(rxChannelCombo, win.opt("AudioProc.Rx.ChannelSelect", 0))
                    onActivated: win.setOpt("AudioProc.Rx.ChannelSelect", currentValue)
                }
            }

            GroupBox {
                title: qsTr("Noise Reduction Algorithm")
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    RadioButton { text: qsTr("None"); checked: nrMode === 0; onClicked: win.setOpt("AudioProc.Rx.NrMode", 0) }
                    RadioButton { text: qsTr("Speex"); checked: nrMode === 1; onClicked: win.setOpt("AudioProc.Rx.NrMode", 1) }
                    RadioButton { text: qsTr("ANR"); checked: nrMode === 2; onClicked: win.setOpt("AudioProc.Rx.NrMode", 2) }
                    Item { Layout.fillWidth: true }
                }
            }

            Label {
                visible: nrMode === 0
                text: qsTr("No noise reduction selected.\nAudio passes through to the equalizer and output gain.")
                font.italic: true
            }

            CollapsibleSection {
                visible: nrMode === 1
                title: qsTr("Speex DSP Noise Reduction")
                ColumnLayout {
                    anchors.fill: parent
                    SliderRow { label: qsTr("Suppression"); key: "AudioProc.Rx.SpeexSuppression"; from: -70; to: -1; step: 1; suffix: " dB"; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("Bark bands"); key: "AudioProc.Rx.SpeexBandsPreset"; from: 0; to: 8; step: 1; maxSliderWidth: 260 }
                    ComboRow { label: qsTr("Frame size"); key: "AudioProc.Rx.SpeexFrameMs"; values: [{ text: qsTr("10 ms frames"), value: 10 }, { text: qsTr("20 ms frames"), value: 20 }] }
                    SwitchRow { text: qsTr("Enable automatic gain control (AGC)"); key: "AudioProc.Rx.SpeexAgc" }
                    SliderRow { label: qsTr("AGC level"); key: "AudioProc.Rx.SpeexAgcLevel"; from: 500; to: 32000; step: 100; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("AGC max gain"); key: "AudioProc.Rx.SpeexAgcMaxGain"; from: 0; to: 60; step: 1; suffix: " dB"; maxSliderWidth: 260 }
                    SwitchRow { text: qsTr("Enable voice activity detection (VAD)"); key: "AudioProc.Rx.SpeexVad" }
                    SliderRow { label: qsTr("VAD start"); key: "AudioProc.Rx.SpeexVadProbStart"; from: 0; to: 100; step: 1; suffix: " %"; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("VAD continue"); key: "AudioProc.Rx.SpeexVadProbCont"; from: 0; to: 100; step: 1; suffix: " %"; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("SNR decay"); key: "AudioProc.Rx.SpeexSnrDecay"; from: 0; to: 0.95; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("Noise update"); key: "AudioProc.Rx.SpeexNoiseUpdateRate"; from: 0.01; to: 0.5; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("Prior base"); key: "AudioProc.Rx.SpeexPriorBase"; from: 0.05; to: 0.5; step: 0.01; decimals: 2; maxSliderWidth: 260 }
                }
            }

            CollapsibleSection {
                visible: nrMode === 2
                title: qsTr("ANR Noise Reduction")
                ColumnLayout {
                    anchors.fill: parent
                    SliderRow { label: qsTr("Noise reduction"); key: "AudioProc.Rx.AnrNoiseReductionDb"; from: 0; to: 48; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("Sensitivity"); key: "AudioProc.Rx.AnrSensitivity"; from: 0; to: 10; step: 0.1; decimals: 1; maxSliderWidth: 260 }
                    SliderRow { label: qsTr("Smoothing"); key: "AudioProc.Rx.AnrFreqSmoothing"; from: 0; to: 6; step: 1; maxSliderWidth: 260 }
                    RowLayout {
                        Button { text: qsTr("Collect Noise Sample"); onClicked: MainController.startAnrNoiseProfile() }
                        Button { text: qsTr("Stop Collecting"); onClicked: MainController.stopAnrNoiseProfile() }
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Receive Equalizer")
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        SwitchRow { text: qsTr("Enable EQ"); key: "AudioProc.Rx.EqEnabled" }
                        Button {
                            text: qsTr("Clear")
                            onClicked: {
                                for (var i = 0; i < 4; ++i)
                                    win.setOpt("AudioProc.Rx.EqGain" + i, 0)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: [
                                { name: qsTr("Low Shelf"), freq: "100 Hz" },
                                { name: qsTr("Low Mid"), freq: "800 Hz" },
                                { name: qsTr("High Mid"), freq: "2.00 kHz" },
                                { name: qsTr("High Shelf"), freq: "3.50 kHz" }
                            ]
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.name; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                                Label { text: Number(win.opt("AudioProc.Rx.EqGain" + index, 0)).toFixed(1) + " dB"; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                                Slider {
                                    orientation: Qt.Vertical
                                    from: -9
                                    to: 9
                                    stepSize: 0.5
                                    value: Number(win.opt("AudioProc.Rx.EqGain" + index, 0))
                                    Layout.preferredHeight: 104
                                    Layout.alignment: Qt.AlignHCenter
                                    onMoved: win.setOpt("AudioProc.Rx.EqGain" + index, value)
                                }
                                Slider {
                                    from: 20
                                    to: 8000
                                    stepSize: 10
                                    value: Number(win.opt("AudioProc.Rx.EqFreq" + index, 0))
                                    Layout.preferredWidth: 28
                                    Layout.alignment: Qt.AlignHCenter
                                    onMoved: win.setOpt("AudioProc.Rx.EqFreq" + index, value)
                                }
                                Label { text: modelData.freq; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Output Gain")
                Layout.fillWidth: true
                SliderRow { label: qsTr("Output gain"); key: "AudioProc.Rx.OutputGainDB"; from: -6; to: 20; step: 0.5; decimals: 1; suffix: " dB"; maxSliderWidth: 86 }
            }

            SpectrumBox {
                title: qsTr("RX Spectrum")
                enableKey: "AudioProc.Rx.SpectrumEnabled"
                inhibitKey: "AudioProc.Rx.SpecInhibitDuringTx"
                fpsKey: "AudioProc.Rx.SpectrumFPS"
                blocksProcessed: win.rxBlocks
                processorEnabled: win.rxSpectrumEnabled
                inputBins: win.rxInBins
                outputBins: win.rxOutBins
            }
        }
    }
}
