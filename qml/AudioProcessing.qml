import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

// TX / RX audio processing window.
//
// Sizing rule: nothing in here sets a fixed pixel width on a container. Each
// control reports its implicit size, the layouts add those up, and the window
// follows the page's implicit size (see fitToContent). Collapsing a section or
// switching the RX noise-reduction mode changes the page's implicit size, and
// the window grows or shrinks to match. A vertical scroll bar only appears
// when the content is taller than the screen allows.
ApplicationWindow {
    id: win
    transientParent: null
    title: mode === "rx" ? qsTr("RX Audio Processing") : qsTr("TX Audio Processing")
    color: palette.window

    readonly property int pageMargin: 8
    readonly property real vScrollBarWidth: scrollView.ScrollBar.vertical ? scrollView.ScrollBar.vertical.width : 0
    readonly property real naturalWidth: Math.ceil(page.implicitWidth) + 2 * pageMargin + vScrollBarWidth
    readonly property real naturalHeight: Math.ceil(page.implicitHeight) + 2 * pageMargin
    readonly property real screenHeightLimit: Screen.desktopAvailableHeight > 0
                                              ? Screen.desktopAvailableHeight - 40 : 4096
    // Width the window was last fitted to; a wider window means the user
    // widened it by hand, and that choice is kept when the content narrows.
    property real fittedWidth: 0

    minimumWidth: naturalWidth
    minimumHeight: page.implicitHeight > 0 ? Math.min(naturalHeight, 200) : 200
    width: 600
    height: 480

    function fitToContent() {
        if (page.implicitHeight <= 0)
            return
        var userWidened = fittedWidth > 0 && width > fittedWidth + 1
        if (!userWidened || width < naturalWidth)
            width = naturalWidth
        if (width <= naturalWidth + 1)
            fittedWidth = naturalWidth
        height = Math.min(naturalHeight, screenHeightLimit)
    }

    Timer {
        id: fitTimer
        interval: 20
        onTriggered: win.fitToContent()
    }

    // Second pass shortly after the window is shown: fonts, palette and
    // style can still nudge implicit sizes once the window is exposed.
    Timer {
        id: settleTimer
        interval: 250
        onTriggered: win.fitToContent()
    }
    onNaturalWidthChanged: fitTimer.restart()
    onNaturalHeightChanged: fitTimer.restart()

    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true

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

    function applyDisabledPalette() {
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
            // Qt 5 does not expose palette disabled groups to QML.
        }
    }

    Component.onCompleted: {
        applyDisabledPalette()
        fitToContent()
    }

    Connections {
        target: MainController.settings
        function onColChanged(items) {
            win.applyDisabledPalette()
        }
    }

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

    // Receive EQ bands (AudioProc.Rx.EqGainN / EqFreqN). minFreq/maxFreq set
    // the travel of each band's frequency knob.
    readonly property var rxEqBands: [
        { name: qsTr("Low Shelf"),  minFreq: 20,   maxFreq: 1000 },
        { name: qsTr("Low Mid"),    minFreq: 100,  maxFreq: 3000 },
        { name: qsTr("High Mid"),   minFreq: 300,  maxFreq: 6000 },
        { name: qsTr("High Shelf"), minFreq: 1000, maxFreq: 8000 }
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

    // setOption() notifies SettingsController::audioProcChanged, which
    // MainController already routes to applyAudioProcessingSettings().
    function setOpt(key, value) {
        if (!controller)
            return
        controller.setOption(key, value)
    }

    onVisibleChanged: {
        if (visible) {
            MainController.updateApplicationPalette()
            applyDisabledPalette()
            fitTimer.restart()
            settleTimer.restart()
        }
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

    // Log-scale mapping between a 0..1 knob position and a frequency.
    function posToFreq(pos, minFreq, maxFreq) {
        var f = minFreq * Math.pow(maxFreq / minFreq, Math.max(0, Math.min(1, pos)))
        var step = f < 100 ? 1 : (f < 1000 ? 5 : (f < 3000 ? 10 : 50))
        return Math.round(f / step) * step
    }

    function freqToPos(freq, minFreq, maxFreq) {
        var f = Math.max(minFreq, Math.min(maxFreq, Number(freq)))
        return Math.log(f / minFreq) / Math.log(maxFreq / minFreq)
    }

    function formatHz(freq) {
        if (freq >= 1000)
            return (freq / 1000).toFixed(2) + " kHz"
        return Math.round(freq) + " Hz"
    }

    // Sliders and knobs that respond to the mouse wheel (see WheelArea.qml).
    component WheelSlider: Slider { WheelArea {} }
    component WheelDial: Dial { WheelArea {} }

    component SwitchRow: CheckBox {
        id: switchRow
        property string key: ""
        // Keyed switches reflect/persist a setting. Keyless switches are plain
        // runtime checkboxes — the caller binds checked and handles onToggled.
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

    // A column of SliderRow / ComboRow items whose label and value columns
    // line up. The shared widths are the widest text actually present, not a
    // fixed pixel size.
    component Form: ColumnLayout {
        id: form
        spacing: 2
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        readonly property real labelWidth: maxChild("labelImplicitWidth")
        readonly property real valueWidth: maxChild("valueImplicitWidth")
        function maxChild(prop) {
            var w = 0
            for (var i = 0; i < children.length; ++i) {
                var c = children[i]
                if (c && c[prop] !== undefined)
                    w = Math.max(w, c[prop])
            }
            return w
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
        property real displayScale: 1
        readonly property real labelImplicitWidth: labelItem.implicitWidth
        // Widest readout the slider can produce, so the value column does not
        // jitter as the number changes.
        readonly property real valueImplicitWidth: Math.ceil(Math.max(fromMetrics.width, toMetrics.width)) + 2
        readonly property var form: parent && parent.labelWidth !== undefined ? parent : null
        function format(v) { return Number(v * row.displayScale).toFixed(row.decimals) + row.suffix }

        spacing: 6
        Layout.fillWidth: true

        Label {
            id: labelItem
            text: row.label
            Layout.preferredWidth: row.form ? row.form.labelWidth : implicitWidth
        }
        WheelSlider {
            id: slider
            Layout.fillWidth: true
            Layout.minimumWidth: 70
            Layout.preferredWidth: 130
            from: row.from
            to: row.to
            stepSize: row.step
            value: Number(win.opt(row.key, row.from))
            onMoved: win.setOpt(row.key, value)
        }
        Label {
            id: valueItem
            horizontalAlignment: Text.AlignRight
            text: row.format(slider.value)
            Layout.preferredWidth: row.form ? row.form.valueWidth : row.valueImplicitWidth
        }
        TextMetrics { id: fromMetrics; font: valueItem.font; text: row.format(row.from) }
        TextMetrics { id: toMetrics; font: valueItem.font; text: row.format(row.to) }
    }

    component ComboRow: RowLayout {
        id: comboRow
        property string key: ""
        property string label: ""
        property var values: []
        readonly property real labelImplicitWidth: comboLabel.implicitWidth
        readonly property var form: parent && parent.labelWidth !== undefined ? parent : null
        spacing: 6
        Layout.fillWidth: true
        Label {
            id: comboLabel
            text: comboRow.label
            Layout.preferredWidth: comboRow.form ? comboRow.form.labelWidth : implicitWidth
        }
        ComboBox {
            id: combo
            textRole: "text"
            valueRole: "value"
            model: comboRow.values
            currentIndex: win.comboIndex(combo, win.opt(comboRow.key, 0))
            onActivated: win.setOpt(comboRow.key, currentValue)
        }
        Item { Layout.fillWidth: true }
    }

    // One TX multiband EQ band: value, vertical gain slider, frequency.
    // Columns are only as wide as their text, so all bands fit side by side.
    component VerticalEq: ColumnLayout {
        id: eq
        property string key: ""
        property string label: ""
        spacing: 0
        Layout.fillWidth: true
        // A ColumnLayout's maximum width otherwise defaults to its widest
        // child, which would stop fillWidth from spreading the bands out.
        Layout.maximumWidth: Number.POSITIVE_INFINITY
        Label {
            text: Number(slider.value).toFixed(1)
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: eqValueMetrics.width + 2
            TextMetrics { id: eqValueMetrics; font.pixelSize: 10; text: "-9.0" }
        }
        WheelSlider {
            id: slider
            orientation: Qt.Vertical
            Layout.preferredHeight: 100
            Layout.alignment: Qt.AlignHCenter
            from: -9
            to: 9
            stepSize: 0.5
            value: Number(win.opt(eq.key, 0))
            onMoved: win.setOpt(eq.key, value)
        }
        Label {
            text: eq.label
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // One RX EQ band: gain readout, vertical gain slider, and a small
    // frequency knob underneath with its live readout.
    component RxEqBand: ColumnLayout {
        id: band
        property int bandIndex: 0
        property string name: ""
        property real minFreq: 20
        property real maxFreq: 8000
        readonly property string gainKey: "AudioProc.Rx.EqGain" + bandIndex
        readonly property string freqKey: "AudioProc.Rx.EqFreq" + bandIndex
        spacing: 1
        Layout.fillWidth: true
        Layout.maximumWidth: Number.POSITIVE_INFINITY

        Label {
            text: band.name
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: Number(gainSlider.value).toFixed(1) + " dB"
            font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }
        WheelSlider {
            id: gainSlider
            orientation: Qt.Vertical
            from: -9
            to: 9
            stepSize: 0.5
            value: Number(win.opt(band.gainKey, 0))
            Layout.preferredHeight: 90
            Layout.alignment: Qt.AlignHCenter
            onMoved: win.setOpt(band.gainKey, value)
        }
        WheelDial {
            id: freqDial
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            Layout.alignment: Qt.AlignHCenter
            inputMode: Dial.Vertical
            from: 0
            to: 1
            value: win.freqToPos(win.opt(band.freqKey, band.minFreq), band.minFreq, band.maxFreq)
            onMoved: win.setOpt(band.freqKey, win.posToFreq(value, band.minFreq, band.maxFreq))
        }
        Label {
            text: win.formatHz(win.posToFreq(freqDial.value, band.minFreq, band.maxFreq))
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: freqMetrics.width + 2
            TextMetrics { id: freqMetrics; font.pixelSize: 11; text: "8.00 kHz" }
        }
    }

    component LevelMeter: ColumnLayout {
        id: meter
        property string label: ""
        property real value: 0
        // Fill from the right instead of the left.
        property bool reversed: false
        spacing: 1
        Layout.fillWidth: true
        Label { text: meter.label; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
        ProgressBar {
            id: meterBar
            Layout.fillWidth: true
            transform: Scale { origin.x: meterBar.width / 2; xScale: meter.reversed ? -1 : 1 }
            Layout.preferredWidth: 80
            Layout.preferredHeight: 12
            from: 0
            to: 100
            value: Math.max(0, Math.min(100, meter.value * 100))
        }
    }

    // Static plot of the ANR noise profile (dB per linear FFT bin) on the
    // same log frequency axis as the spectrum views. Auto-scales vertically
    // to the profile and repaints only when a new profile arrives.
    component NoiseProfilePlot: Canvas {
        id: plot
        property var bins: []
        property real sampleRate: 0
        property int windowSize: 0
        property string lineColor: "#5aa9ff"
        readonly property bool hasData: bins && bins.length > 1 && sampleRate > 0 && windowSize > 0

        Layout.fillWidth: true
        Layout.preferredWidth: 240
        Layout.preferredHeight: 110
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onBinsChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var left = 34, right = 6, top = 6, bottom = 18
            var plotX = left, plotY = top
            var plotW = Math.max(1, width - left - right)
            var plotH = Math.max(1, height - top - bottom)
            var bgColor = win.canvasColor(MainController.settings.options["Color.PlotBackground"], 1)
            var gridColor = win.canvasColor(MainController.settings.options["Color.Grid"], 1)
            var textColor = win.canvasColor(win.palette.text, 1)
            var subtleTextColor = win.canvasColor(win.palette.text, 0.75)

            ctx.fillStyle = bgColor
            ctx.fillRect(0, 0, width, height)
            if (!plot.hasData)
                return

            var fMin = 50
            var fMax = Math.max(fMin * 2, Math.min(8000, plot.sampleRate / 2))
            var binHz = plot.sampleRate / plot.windowSize

            // Auto-scale to the profile range, padded and snapped to 6 dB.
            var lo = Infinity, hi = -Infinity
            for (var i = 0; i < bins.length; ++i) {
                var f = i * binHz
                if (f < fMin || f > fMax) continue
                var v = Number(bins[i])
                if (v < lo) lo = v
                if (v > hi) hi = v
            }
            if (!isFinite(lo) || !isFinite(hi))
                return
            var range = Math.max(6, hi - lo)
            var margin = range * 0.1
            var minDb = Math.floor((lo - margin) / 6) * 6
            var maxDb = Math.ceil((hi + margin) / 6) * 6

            function dbToY(db) {
                return plotY + plotH - ((db - minDb) / (maxDb - minDb)) * plotH
            }
            function freqToX(freq) {
                var logMin = Math.log(fMin), logMax = Math.log(fMax)
                return plotX + ((Math.log(Math.max(fMin, freq)) - logMin) / (logMax - logMin)) * plotW
            }
            function freqLabel(freq) {
                if (freq >= 1000)
                    return (freq / 1000).toFixed(freq % 1000 === 0 ? 0 : 1) + "k"
                return String(freq)
            }

            // Grid
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
                ctx.fillText(String(db), plotX - 4, y)
            }
            var freqs = plotW < 260 ? [50, 200, 800, 3200, 8000]
                                    : [50, 100, 200, 400, 800, 1600, 3200, 6400, 8000]
            ctx.textAlign = "center"
            for (var k = 0; k < freqs.length; ++k) {
                if (freqs[k] > fMax) continue
                var x = freqToX(freqs[k])
                ctx.beginPath()
                ctx.moveTo(x, plotY)
                ctx.lineTo(x, plotY + plotH)
                ctx.stroke()
                ctx.fillText(freqLabel(freqs[k]), x, plotY + plotH + 10)
            }
            ctx.setLineDash([])

            // Profile curve
            ctx.save()
            ctx.beginPath()
            ctx.rect(plotX, plotY, plotW, plotH)
            ctx.clip()
            ctx.strokeStyle = plot.lineColor
            ctx.lineWidth = 1.5
            ctx.beginPath()
            var started = false
            for (var n = 1; n < bins.length; ++n) {
                var fn = n * binHz
                if (fn < fMin * 0.9) continue
                var px = freqToX(fn)
                var py = dbToY(Number(bins[n]))
                if (!started) { ctx.moveTo(px, py); started = true }
                else ctx.lineTo(px, py)
                if (fn > fMax) break
            }
            ctx.stroke()
            ctx.restore()

            ctx.strokeStyle = textColor
            ctx.beginPath()
            ctx.rect(plotX, plotY, plotW, plotH)
            ctx.stroke()

            ctx.fillStyle = plot.lineColor
            ctx.textAlign = "right"
            ctx.textBaseline = "top"
            ctx.fillText(qsTr("Noise floor"), plotX + plotW - 4, plotY + 3)
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
        property string inhibitText: ""
        property string inhibitedMessage: ""
        // true: the inhibit setting pauses this spectrum while transmitting
        // (RX); false: it pauses while receiving (TX).
        property bool inhibitWhileTransmitting: false
        property string fpsKey: ""
        property int blocksProcessed: 0
        property bool processorEnabled: false
        readonly property bool spectrumEnabled: enableKey === "" || win.optBool(enableKey, false)
        readonly property bool inhibited: inhibitKey !== "" && win.optBool(inhibitKey, false)
                                          && (inhibitWhileTransmitting ? MainController.transmitting
                                                                       : !MainController.transmitting)

        headerItems: SwitchRow { text: qsTr("Enable"); key: box.enableKey }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            SwitchRow { text: box.inhibitText; key: box.inhibitKey }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("FPS") }
            SpinBox {
                from: 1
                to: 60
                value: Number(win.opt(box.fpsKey, 10))
                onValueModified: win.setOpt(box.fpsKey, value)
            }
        }

        // Kept deliberately small: it follows the window width but never
        // asks for more than a modest width or height on its own.
        Canvas {
            id: canvas
            Layout.fillWidth: true
            Layout.preferredWidth: 240
            Layout.preferredHeight: 110
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var minDb = -90
                var maxDb = 0
                var left = 34
                var right = 6
                var top = 6
                var bottom = 18
                var plotX = left
                var plotY = top
                var plotW = Math.max(1, width - left - right)
                var plotH = Math.max(1, height - top - bottom)
                var bgColor = win.canvasColor(MainController.settings.options["Color.PlotBackground"], 1)
                var gridColor = win.canvasColor(MainController.settings.options["Color.Grid"], 1)
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
                        ctx.fillText(String(db), plotX - 4, y)
                    }

                    // Thin out frequency labels when the plot is narrow.
                    var freqs = plotW < 260 ? [50, 200, 800, 3200, 8000]
                                            : [50, 100, 200, 400, 800, 1600, 3200, 6400, 8000]
                    ctx.textAlign = "center"
                    for (var f = 0; f < freqs.length; ++f) {
                        var x = freqToX(freqs[f])
                        ctx.beginPath()
                        ctx.moveTo(x, plotY)
                        ctx.lineTo(x, plotY + plotH)
                        ctx.stroke()
                        ctx.fillText(freqLabel(freqs[f]), x, plotY + plotH + 10)
                    }
                    ctx.setLineDash([])

                    ctx.strokeStyle = textColor
                    ctx.beginPath()
                    ctx.rect(plotX, plotY, plotW, plotH)
                    ctx.stroke()
                    ctx.restore()
                }

                function message(text) {
                    ctx.fillStyle = textColor
                    ctx.textAlign = "center"
                    ctx.textBaseline = "middle"
                    ctx.font = "11px sans-serif"
                    ctx.fillText(text, plotX + plotW / 2, plotY + plotH / 2)
                }

                if (!box.spectrumEnabled) {
                    message(qsTr("Spectrum disabled"))
                    return
                }
                if (box.inhibited) {
                    message(box.inhibitedMessage)
                    return
                }
                if ((!box.inputBins || box.inputBins.length < 2) && (!box.outputBins || box.outputBins.length < 2)) {
                    message(qsTr("Waiting for audio (blocks: %1)").arg(box.blocksProcessed))
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
        onInputBinsChanged: canvas.requestPaint()
        onOutputBinsChanged: canvas.requestPaint()
        onSpectrumEnabledChanged: canvas.requestPaint()
        onInhibitedChanged: canvas.requestPaint()
        onBlocksProcessedChanged: if (inputBins.length < 2) canvas.requestPaint()
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
        clip: true
        contentWidth: availableWidth
        contentHeight: win.naturalHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: win.naturalHeight > scrollView.height + 1 ? ScrollBar.AlwaysOn
                                                                             : ScrollBar.AlwaysOff

        // The page reports its implicit size up to the window; the window
        // never pushes a fixed size down into the page.
        Loader {
            id: page
            x: win.pageMargin
            y: win.pageMargin
            width: scrollView.availableWidth - 2 * win.pageMargin - win.vScrollBarWidth
            height: implicitHeight
            sourceComponent: win.txMode ? txPage : rxPage
        }
    }

    Component {
        id: txPage
        ColumnLayout {
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SwitchRow { text: qsTr("Master Bypass"); key: "AudioProc.Tx.Bypass"; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Order") }
                ComboBox {
                    id: orderCombo
                    textRole: "text"
                    valueRole: "value"
                    model: [{ text: qsTr("EQ -> Compressor"), value: true }, { text: qsTr("Compressor -> EQ"), value: false }]
                    currentIndex: win.comboIndex(orderCombo, win.opt("AudioProc.Tx.EqFirst", true))
                    onActivated: win.setOpt("AudioProc.Tx.EqFirst", currentValue)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Form {
                    SliderRow { label: qsTr("Input gain"); key: "AudioProc.Tx.InputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB" }
                }
                Form {
                    SliderRow { label: qsTr("Output gain"); key: "AudioProc.Tx.OutputGainDB"; from: -20; to: 20; step: 0.5; decimals: 1; suffix: " dB" }
                }
            }

            CollapsibleSection {
                title: qsTr("Noise Gate")
                expanded: false
                headerItems: SwitchRow { text: qsTr("Enable"); key: "AudioProc.Tx.GateEnabled" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Form {
                        SliderRow { label: qsTr("Threshold"); key: "AudioProc.Tx.GateThreshold"; from: -70; to: 0; step: 1; suffix: " dB" }
                        SliderRow { label: qsTr("Range"); key: "AudioProc.Tx.GateRange"; from: -90; to: 0; step: 1; suffix: " dB" }
                        SliderRow { label: qsTr("Attack"); key: "AudioProc.Tx.GateAttack"; from: 1; to: 500; step: 1; suffix: " ms" }
                        SliderRow { label: qsTr("Hold"); key: "AudioProc.Tx.GateHold"; from: 2; to: 1000; step: 2; suffix: " ms" }
                    }
                    Form {
                        SliderRow { label: qsTr("Decay"); key: "AudioProc.Tx.GateDecay"; from: 2; to: 1000; step: 2; suffix: " ms" }
                        SliderRow { label: qsTr("LF cutoff"); key: "AudioProc.Tx.GateLfCutoff"; from: 20; to: 500; step: 5; suffix: " Hz" }
                        SliderRow { label: qsTr("HF cutoff"); key: "AudioProc.Tx.GateHfCutoff"; from: 200; to: 4000; step: 10; suffix: " Hz" }
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Multiband EQ")
                expanded: false
                headerItems: [
                    SwitchRow { text: qsTr("Enable"); key: "AudioProc.Tx.EqEnabled" },
                    Button {
                        text: qsTr("Clear")
                        flat: true
                        onClicked: {
                            for (var i = 0; i < win.eqBands.length; ++i)
                                win.setOpt("AudioProc.Tx.EqBand" + i, 0)
                        }
                    }
                ]
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: win.visibleEqBands()
                        VerticalEq { key: "AudioProc.Tx.EqBand" + modelData.idx; label: modelData.label }
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Dyson Compressor")
                headerItems: SwitchRow { text: qsTr("Enable"); key: "AudioProc.Tx.CompEnabled" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Form {
                        SliderRow { label: qsTr("Peak limit"); key: "AudioProc.Tx.CompPeakLimit"; from: -30; to: 0; step: 0.5; decimals: 1; suffix: " dB" }
                        SliderRow { label: qsTr("Release"); key: "AudioProc.Tx.CompRelease"; from: 0.01; to: 1; step: 0.01; decimals: 2; suffix: " s" }
                    }
                    Form {
                        SliderRow { label: qsTr("Fast ratio"); key: "AudioProc.Tx.CompFastRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                        SliderRow { label: qsTr("Slow ratio"); key: "AudioProc.Tx.CompSlowRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Self-Monitor")
                headerItems: SwitchRow { text: qsTr("Enable"); key: "AudioProc.Tx.SidetoneEnabled" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    SwitchRow {
                        text: qsTr("Mute RX audio")
                        // Runtime-only; MainController clears it when
                        // self-monitoring is turned off.
                        enabled: win.optBool("AudioProc.Tx.SidetoneEnabled", false)
                        checked: MainController.rxMuted
                        onToggled: MainController.setRxMuted(checked)
                    }
                    SliderRow { label: qsTr("Level"); key: "AudioProc.Tx.SidetoneLevel"; from: 0; to: 1; step: 0.01; decimals: 0; suffix: "%"; displayScale: 100 }
                }
            }

            SpectrumBox {
                title: qsTr("TX Spectrum")
                enableKey: "AudioProc.Tx.SpectrumEnabled"
                inhibitKey: "AudioProc.Tx.SpecInhibitDuringRx"
                inhibitText: qsTr("Pause while receiving")
                inhibitedMessage: qsTr("Paused while receiving")
                inhibitWhileTransmitting: false
                fpsKey: "AudioProc.Tx.SpectrumFPS"
                blocksProcessed: win.txBlocks
                processorEnabled: win.txSpectrumEnabled
                inputBins: win.txInBins
                outputBins: win.txOutBins
            }

            CollapsibleSection {
                title: qsTr("Meters")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    LevelMeter { label: qsTr("Input"); value: win.txInputLevel }
                    LevelMeter { label: qsTr("Output"); value: win.txOutputLevel }
                    LevelMeter {
                        label: qsTr("Gain Reduction")
                        value: win.txGainReduction
                        reversed: {
                            var v = MainController.settings ? MainController.settings.options["Interface.CompMeterReverse"] : false
                            return v === true || v === "true"
                        }
                    }
                }
            }
        }
    }

    Component {
        id: rxPage
        ColumnLayout {
            spacing: 6
            readonly property int nrMode: Number(win.opt("AudioProc.Rx.NrMode", 0))

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SwitchRow { text: qsTr("Master Bypass"); key: "AudioProc.Rx.Bypass"; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Channel") }
                ComboBox {
                    id: rxChannelCombo
                    textRole: "text"
                    valueRole: "value"
                    model: [{ text: qsTr("Auto"), value: 0 }, { text: qsTr("Left"), value: 1 }, { text: qsTr("Right"), value: 2 }, { text: qsTr("Mono"), value: 3 }]
                    currentIndex: win.comboIndex(rxChannelCombo, win.opt("AudioProc.Rx.ChannelSelect", 0))
                    onActivated: win.setOpt("AudioProc.Rx.ChannelSelect", currentValue)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("Noise reduction") }
                RadioButton { text: qsTr("None"); checked: nrMode === 0; onClicked: win.setOpt("AudioProc.Rx.NrMode", 0) }
                RadioButton { text: qsTr("Speex"); checked: nrMode === 1; onClicked: win.setOpt("AudioProc.Rx.NrMode", 1) }
                RadioButton { text: qsTr("ANR"); checked: nrMode === 2; onClicked: win.setOpt("AudioProc.Rx.NrMode", 2) }
                Item { Layout.fillWidth: true }
            }

            Form {
                SliderRow { label: qsTr("Output gain"); key: "AudioProc.Rx.OutputGainDB"; from: -6; to: 20; step: 0.5; decimals: 1; suffix: " dB" }
            }

            CollapsibleSection {
                visible: nrMode === 1
                title: qsTr("Speex Noise Reduction")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Form {
                        SliderRow { label: qsTr("Suppression"); key: "AudioProc.Rx.SpeexSuppression"; from: -70; to: -1; step: 1; suffix: " dB" }
                        SliderRow { label: qsTr("Bark bands"); key: "AudioProc.Rx.SpeexBandsPreset"; from: 0; to: 8; step: 1 }
                        ComboRow { label: qsTr("Frame size"); key: "AudioProc.Rx.SpeexFrameMs"; values: [{ text: qsTr("10 ms"), value: 10 }, { text: qsTr("20 ms"), value: 20 }] }
                        SliderRow { label: qsTr("SNR decay"); key: "AudioProc.Rx.SpeexSnrDecay"; from: 0; to: 0.95; step: 0.01; decimals: 2 }
                        SliderRow { label: qsTr("Noise update"); key: "AudioProc.Rx.SpeexNoiseUpdateRate"; from: 0.01; to: 0.5; step: 0.01; decimals: 2 }
                        SliderRow { label: qsTr("Prior base"); key: "AudioProc.Rx.SpeexPriorBase"; from: 0.05; to: 0.5; step: 0.01; decimals: 2 }
                    }
                    Form {
                        SwitchRow { id: agcSwitch; text: qsTr("Automatic gain control"); key: "AudioProc.Rx.SpeexAgc" }
                        SliderRow { label: qsTr("AGC level"); key: "AudioProc.Rx.SpeexAgcLevel"; from: 500; to: 32000; step: 100; enabled: agcSwitch.checked }
                        SliderRow { label: qsTr("AGC max gain"); key: "AudioProc.Rx.SpeexAgcMaxGain"; from: 0; to: 60; step: 1; suffix: " dB"; enabled: agcSwitch.checked }
                        SwitchRow { id: vadSwitch; text: qsTr("Voice activity detection"); key: "AudioProc.Rx.SpeexVad" }
                        SliderRow { label: qsTr("VAD start"); key: "AudioProc.Rx.SpeexVadProbStart"; from: 0; to: 100; step: 1; suffix: " %"; enabled: vadSwitch.checked }
                        SliderRow { label: qsTr("VAD continue"); key: "AudioProc.Rx.SpeexVadProbCont"; from: 0; to: 100; step: 1; suffix: " %"; enabled: vadSwitch.checked }
                    }
                }
            }

            CollapsibleSection {
                visible: nrMode === 2
                title: qsTr("ANR Noise Reduction")
                Form {
                    SliderRow { label: qsTr("Noise reduction"); key: "AudioProc.Rx.AnrNoiseReductionDb"; from: 0; to: 48; step: 0.5; decimals: 1; suffix: " dB" }
                    SliderRow { label: qsTr("Sensitivity"); key: "AudioProc.Rx.AnrSensitivity"; from: 0; to: 10; step: 0.1; decimals: 1 }
                    SliderRow { label: qsTr("Smoothing"); key: "AudioProc.Rx.AnrFreqSmoothing"; from: 0; to: 6; step: 1 }
                }
                RowLayout {
                    spacing: 6
                    Button { text: qsTr("Collect Noise Sample"); onClicked: MainController.startAnrNoiseProfile() }
                    Button {
                        text: qsTr("Stop Collecting")
                        enabled: MainController.anrProfiling
                        onClicked: MainController.stopAnrNoiseProfile()
                    }
                    Label {
                        id: anrStatusLabel
                        Layout.leftMargin: 4
                        // Reserve the widest of the possible messages so the
                        // window doesn't change width as the status changes.
                        Layout.minimumWidth: Math.ceil(Math.max(anrStatusW1.advanceWidth,
                                                                anrStatusW2.advanceWidth,
                                                                anrStatusW3.advanceWidth))
                        TextMetrics { id: anrStatusW1; font: anrStatusLabel.font; text: qsTr("Sample in progress (%1)").arg(5) }
                        TextMetrics { id: anrStatusW2; font: anrStatusLabel.font; text: qsTr("Sample available") }
                        TextMetrics { id: anrStatusW3; font: anrStatusLabel.font; text: qsTr("No sample available") }
                        text: MainController.anrProfiling
                              ? qsTr("Sample in progress (%1)").arg(MainController.anrProfileSecondsLeft)
                              : MainController.anrHasProfile ? qsTr("Sample available")
                                                             : qsTr("No sample available")
                    }
                }
                NoiseProfilePlot {
                    visible: MainController.anrHasProfile && hasData
                    bins: MainController.anrNoiseProfileBins
                    sampleRate: MainController.anrNoiseProfileSampleRate
                    windowSize: MainController.anrNoiseProfileWindowSize
                }
            }

            CollapsibleSection {
                title: qsTr("Receive Equalizer")
                headerItems: [
                    SwitchRow { text: qsTr("Enable"); key: "AudioProc.Rx.EqEnabled" },
                    Button {
                        text: qsTr("Clear")
                        flat: true
                        onClicked: {
                            for (var i = 0; i < win.rxEqBands.length; ++i)
                                win.setOpt("AudioProc.Rx.EqGain" + i, 0)
                        }
                    }
                ]
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: win.rxEqBands
                        RxEqBand {
                            bandIndex: index
                            name: modelData.name
                            minFreq: modelData.minFreq
                            maxFreq: modelData.maxFreq
                        }
                    }
                }
            }

            SpectrumBox {
                title: qsTr("RX Spectrum")
                enableKey: "AudioProc.Rx.SpectrumEnabled"
                inhibitKey: "AudioProc.Rx.SpecInhibitDuringTx"
                inhibitText: qsTr("Pause while transmitting")
                inhibitedMessage: qsTr("Paused while transmitting")
                inhibitWhileTransmitting: true
                fpsKey: "AudioProc.Rx.SpectrumFPS"
                blocksProcessed: win.rxBlocks
                processorEnabled: win.rxSpectrumEnabled
                inputBins: win.rxInBins
                outputBins: win.rxOutBins
            }
        }
    }
}
