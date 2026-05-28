import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
//import QtCharts 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: selectRadioWindow
    title: qsTr("Radio Status")
    width: 760
    height: 420
    minimumWidth: 590
    minimumHeight: 295

    // This property will be set from MainWindow
    property var selectRadioController: null

    // Bind window visibility to controller
    visible: selectRadioController ? selectRadioController.visible : false

    component PlotAxis: QtObject {
        property real min: 0
        property real max: 1
        property string titleText: ""
    }

    component PlotSeries: QtObject {
        property var points: []
        property int count: points.length
        property color color: "#ffffff"
        signal changed()

        function append(x, y) {
            var next = points.slice()
            next.push({ "x": x, "y": y })
            points = next
            count = points.length
            changed()
        }

        function remove(index) {
            if (index < 0 || index >= points.length)
                return
            var next = points.slice()
            next.splice(index, 1)
            points = next
            count = points.length
            changed()
        }

        function at(index) {
            return points[index]
        }
    }

    component SimpleLinePlot: Rectangle {
        id: plot
        property string title: ""
        property var series: null
        property var xAxis: null
        property var yAxis: null
        property string unit: "ms"
        property real currentValue: 0
        property bool hasValue: false
        property string emptyText: qsTr("Waiting for data")

        color: palette.base
        border.color: palette.mid
        border.width: 1
        antialiasing: true

        Connections {
            target: plot.series
            ignoreUnknownSignals: true
            function onChanged() { canvas.requestPaint() }
        }

        Canvas {
            id: canvas
            anchors.fill: parent
            anchors.margins: 2

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = plot.color
                ctx.fillRect(0, 0, width, height)

                ctx.font = "bold 11px sans-serif"
                ctx.fillStyle = palette.text
                ctx.textAlign = "center"
                ctx.fillText(plot.title, width / 2, 14)

                var left = 36
                var right = width - 8
                var top = 24
                var bottom = height - 24
                var plotW = Math.max(1, right - left)
                var plotH = Math.max(1, bottom - top)

                ctx.strokeStyle = palette.mid
                ctx.lineWidth = 1
                for (var i = 0; i <= 4; ++i) {
                    var y = top + (plotH * i / 4)
                    ctx.beginPath()
                    ctx.moveTo(left, y)
                    ctx.lineTo(right, y)
                    ctx.stroke()
                }
                for (var xGrid = 0; xGrid <= 4; ++xGrid) {
                    var x = left + (plotW * xGrid / 4)
                    ctx.beginPath()
                    ctx.moveTo(x, top)
                    ctx.lineTo(x, bottom)
                    ctx.stroke()
                }

                ctx.strokeStyle = palette.text
                ctx.strokeRect(left, top, plotW, plotH)

                ctx.font = "10px sans-serif"
                ctx.fillStyle = palette.text
                ctx.textAlign = "right"
                ctx.fillText(plot.yAxis ? plot.yAxis.max.toFixed(0) : "", left - 4, top + 4)
                ctx.fillText(plot.yAxis ? plot.yAxis.min.toFixed(0) : "", left - 4, bottom)
                ctx.textAlign = "left"
                ctx.fillText(plot.unit, left, height - 6)

                if (plot.hasValue) {
                    ctx.textAlign = "right"
                    ctx.fillText(plot.currentValue.toFixed(1) + " " + plot.unit, right, height - 6)
                }

                if (!plot.series || !plot.xAxis || !plot.yAxis || plot.series.count < 2) {
                    ctx.font = "11px sans-serif"
                    ctx.fillStyle = palette.placeholderText
                    ctx.textAlign = "center"
                    ctx.fillText(plot.emptyText, left + plotW / 2, top + plotH / 2)
                    return
                }

                var xSpan = Math.max(1, plot.xAxis.max - plot.xAxis.min)
                var ySpan = Math.max(1, plot.yAxis.max - plot.yAxis.min)
                ctx.strokeStyle = plot.series.color
                ctx.lineWidth = 2
                ctx.save()
                ctx.beginPath()
                ctx.rect(left, top, plotW, plotH)
                ctx.clip()
                ctx.beginPath()

                for (var p = 0; p < plot.series.count; ++p) {
                    var point = plot.series.at(p)
                    var x = left + ((point.x - plot.xAxis.min) / xSpan) * plotW
                    var yValue = Math.max(plot.yAxis.min, Math.min(plot.yAxis.max, point.y))
                    var y = bottom - ((yValue - plot.yAxis.min) / ySpan) * plotH
                    if (p === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }

                ctx.stroke()
                ctx.restore()
            }
        }

        function requestPaint() {
            canvas.requestPaint()
        }

        onWidthChanged: canvas.requestPaint()
        onHeightChanged: canvas.requestPaint()
    }

    // Close handler
    Component.onCompleted: {
        closing.connect(function(close) {
            if (selectRadioController) {
                selectRadioController.visible = false
            }
            close.accepted = true
        })
    }

    // Connect to controller signals
    Connections {
        target: selectRadioController

        function onTimeDifferencePointAdded(counter, time) {
            timeDifferenceSeries.append(counter, time)
            timeDifferenceChart.currentValue = time
            timeDifferenceChart.hasValue = true

            if (timeDifferenceSeries.count > 100) {
                timeDifferenceSeries.remove(0)
            }

            updateXAxis(timeXAxis, counter, 100)
            updateYAxis(timeYAxis, timeDifferenceSeries, 1, -10, 10, false)
            timeDifferenceChart.requestPaint()
        }

        function onWaterfallPointAdded(counter, time) {
            waterfallSeries.append(counter, time)
            waterfallChart.currentValue = time
            waterfallChart.hasValue = true

            if (waterfallSeries.count > 1000) {
                waterfallSeries.remove(0)
            }

            if (selectRadioWindow.visible) {
                updateXAxis(waterfallXAxis, counter, 1000)
                updateYAxis(waterfallYAxis, waterfallSeries, 1, 0, 10, true)
                waterfallChart.requestPaint()
            }
        }

        function onSpectrumPointAdded(counter, time) {
            spectrumSeries.append(counter, time)
            spectrumChart.currentValue = time
            spectrumChart.hasValue = true

            if (spectrumSeries.count > 1000) {
                spectrumSeries.remove(0)
            }

            if (selectRadioWindow.visible) {
                updateXAxis(spectrumXAxis, counter, 1000)
                updateYAxis(spectrumYAxis, spectrumSeries, 1, 0, 50, true)
                spectrumChart.requestPaint()
            }
        }
    }

    function updateXAxis(axis, counter, windowSize) {
        axis.min = Math.max(0, counter - windowSize)
        axis.max = Math.max(windowSize, counter)
    }

    function updateYAxis(axis, series, minimumPadding, defaultMin, defaultMax, clampToZero) {
        if (!series || series.count === 0) {
            axis.min = defaultMin
            axis.max = defaultMax
            return
        }

        var minY = series.at(0).y
        var maxY = minY
        for (var i = 1; i < series.count; ++i) {
            var point = series.at(i)
            if (point.y < minY)
                minY = point.y
            if (point.y > maxY)
                maxY = point.y
        }

        var span = maxY - minY
        var padding = Math.max(minimumPadding, span * 0.12)
        axis.min = Math.floor(minY - padding)
        axis.max = Math.ceil(maxY + padding)

        if (clampToZero)
            axis.min = Math.max(0, axis.min)

        if (axis.min === axis.max) {
            axis.min -= minimumPadding
            axis.max += minimumPadding
        }
    }

    function displayTxState(value) {
        if (!value || value === "none")
            return qsTr("Idle")
        if (value === "local-input")
            return qsTr("Local input")
        if (value === "external-civ")
            return qsTr("External CI-V")
        if (value === "wfview")
            return qsTr("wfview")
        if (value === "wfshare")
            return qsTr("wfshare")
        if (value === "rigctl")
            return qsTr("rigctl")
        if (value === "tci")
            return qsTr("TCI")
        if (value === "radio")
            return qsTr("Radio")
        return value
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Radio selection table
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            Layout.maximumHeight: 110
            spacing: 0

            // Column headers (fixed at top)
            Row {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: 25
                z: 2

                Repeater {
                    model: [qsTr("Rig Name"), qsTr("CI-V"), qsTr("Baud Rate"), qsTr("Current User"), qsTr("User IP Address")]
                    Rectangle {
                        width: radioTable.width / 5
                        height: 25
                        color: selectRadioWindow.palette.mid
                        border.width: 1
                        border.color: selectRadioWindow.palette.dark

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: selectRadioWindow.palette.text
                            font.bold: true
                        }
                    }
                }
            }

            // Table content
            TableView {
                id: radioTable
                Layout.fillWidth: true
                Layout.fillHeight: true

                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: selectRadioController ? selectRadioController.tableModel : null

                delegate: Rectangle {
                    implicitWidth: radioTable.width / 5
                    implicitHeight: 30
                    border.width: 1
                    border.color: "#cccccc"

                    color: {
                        if (model.busy === 1) return "#006400" // darkGreen
                        if (model.busy === 2) return "#8B0000" // darkRed
                        return "#000000" // black
                    }

                    Text {
                        anchors.centerIn: parent
                        text: model.display
                        color: "white"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (model.busy !== 1 && selectRadioController) {
                                selectRadioController.onRadioSelected(model.row)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("TX audio owner:")
                font.bold: true
            }

            Label {
                text: displayTxState(selectRadioController ? selectRadioController.txAudioOwner : "none")
                Layout.preferredWidth: 92
            }

            Label {
                text: qsTr("TX audio source:")
                font.bold: true
            }

            Label {
                text: displayTxState(selectRadioController ? selectRadioController.txAudioSource : "none")
                Layout.fillWidth: true
            }
        }

        // Charts in a row
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180
            spacing: 6

            // UDP Time Difference Chart
            SimpleLinePlot {
                id: timeDifferenceChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Network time delta")
                unit: qsTr("ms")
                xAxis: timeXAxis
                yAxis: timeYAxis
                series: timeDifferenceSeries

                PlotAxis {
                    id: timeXAxis
                    min: 0
                    max: 100
                }

                PlotAxis {
                    id: timeYAxis
                    min: -10
                    max: 10
                    titleText: qsTr("ms")
                }

                PlotSeries {
                    id: timeDifferenceSeries
                    color: "#2196F3"
                }
            }

            // Waterfall Plot Time Chart
            SimpleLinePlot {
                id: waterfallChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Waterfall render time")
                unit: qsTr("ms")
                xAxis: waterfallXAxis
                yAxis: waterfallYAxis
                series: waterfallSeries

                PlotAxis {
                    id: waterfallXAxis
                    min: 0
                    max: 1000
                }

                PlotAxis {
                    id: waterfallYAxis
                    min: -10
                    max: 10
                    titleText: qsTr("ms")
                }

                PlotSeries {
                    id: waterfallSeries
                    color: "#4CAF50"
                }
            }

            // Spectrum Plot Time Chart
            SimpleLinePlot {
                id: spectrumChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Spectrum render time")
                unit: qsTr("ms")
                xAxis: spectrumXAxis
                yAxis: spectrumYAxis
                series: spectrumSeries

                PlotAxis {
                    id: spectrumXAxis
                    min: 0
                    max: 1000
                }

                PlotAxis {
                    id: spectrumYAxis
                    min: 0
                    max: 50
                    titleText: qsTr("ms")
                }

                PlotSeries {
                    id: spectrumSeries
                    color: "#FF9800"
                }
            }
        }

        // Cancel button
        Button {
            id: cancelButton
            text: qsTr("Cancel")
            Layout.alignment: Qt.AlignRight
            onClicked: {
                if (selectRadioController) {
                    selectRadioController.visible = false
                }
            }
        }
    }
}
