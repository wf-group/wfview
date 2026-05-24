import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
//import QtCharts 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: selectRadioWindow
    title: "Select Radio"
    width: 400
    height: 300

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

        color: "#1e1e1e"
        border.color: "#404040"
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
            anchors.margins: 4

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = plot.color
                ctx.fillRect(0, 0, width, height)

                ctx.font = "bold 10px sans-serif"
                ctx.fillStyle = "white"
                ctx.textAlign = "center"
                ctx.fillText(plot.title, width / 2, 12)

                var left = 26
                var right = width - 6
                var top = 20
                var bottom = height - 18
                var plotW = Math.max(1, right - left)
                var plotH = Math.max(1, bottom - top)

                ctx.strokeStyle = "#404040"
                ctx.lineWidth = 1
                for (var i = 0; i <= 4; ++i) {
                    var y = top + (plotH * i / 4)
                    ctx.beginPath()
                    ctx.moveTo(left, y)
                    ctx.lineTo(right, y)
                    ctx.stroke()
                }

                if (!plot.series || !plot.xAxis || !plot.yAxis || plot.series.count < 2)
                    return

                var xSpan = Math.max(1, plot.xAxis.max - plot.xAxis.min)
                var ySpan = Math.max(1, plot.yAxis.max - plot.yAxis.min)
                ctx.strokeStyle = plot.series.color
                ctx.lineWidth = 2
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
            }
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

        function onAudioOutputLevelChanged(level) {
            afLevel.value = level
        }

        function onAudioInputLevelChanged(level) {
            modLevel.value = level
        }

        function onTimeDifferencePointAdded(counter, time) {
            timeDifferenceSeries.append(counter, time)

            if (timeDifferenceSeries.count > 100) {
                timeDifferenceSeries.remove(0)
                timeXAxis.min = counter - 100
                timeXAxis.max = counter
            }

            var minY = 10, maxY = -10
            for (var i = 0; i < timeDifferenceSeries.count; i++) {
                var point = timeDifferenceSeries.at(i)
                if (point.y < minY) minY = point.y
                if (point.y > maxY) maxY = point.y
            }
            timeYAxis.min = Math.floor(minY - 1)
            timeYAxis.max = Math.ceil(maxY + 1)
        }

        function onWaterfallPointAdded(counter, time) {
            waterfallSeries.append(counter, time)

            if (waterfallSeries.count > 1000) {
                waterfallSeries.remove(0)
                waterfallXAxis.min = counter - 1000
                waterfallXAxis.max = counter
            }

            if (selectRadioWindow.visible) {
                var minY = 10, maxY = -10
                for (var i = 0; i < waterfallSeries.count; i++) {
                    var point = waterfallSeries.at(i)
                    if (point.y < minY) minY = point.y
                    if (point.y > maxY) maxY = point.y
                }
                waterfallYAxis.min = Math.floor(minY - 1)
                waterfallYAxis.max = Math.ceil(maxY + 1)
            }
        }

        function onSpectrumPointAdded(counter, time) {
            spectrumSeries.append(counter, time)

            if (spectrumSeries.count > 1000) {
                spectrumSeries.remove(0)
                spectrumXAxis.min = counter - 1000
                spectrumXAxis.max = counter
            }

            if (selectRadioWindow.visible) {
                var minY = 50, maxY = 0
                for (var i = 0; i < spectrumSeries.count; i++) {
                    var point = spectrumSeries.at(i)
                    if (point.y < minY) minY = point.y
                    if (point.y > maxY) maxY = point.y
                }
                spectrumYAxis.min = 0
                spectrumYAxis.max = Math.ceil(maxY + 5)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // Radio selection table
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 60
            spacing: 0

            // Column headers (fixed at top)
            Row {
                id: header
                Layout.fillWidth: true
                height: 25
                z: 2

                Repeater {
                    model: ["Name", "CIV", "Baudrate", "User", "IP"]
                    Rectangle {
                        width: radioTable.width / 5
                        height: 25
                        color: "#404040"
                        border.width: 1
                        border.color: "#666666"

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: "white"
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

        // Audio level meters
        RowLayout {
            Layout.fillWidth: true
            //Layout.height: 30
            spacing: 20

            Label {
                text: "AF Level:"
                //color: "white"
            }

            ProgressBar {
                id: afLevel
                Layout.fillWidth: true
                from: 0
                to: 255
                value: 0
            }

            Label {
                text: "Mod Level:"
                //color: "white"
            }

            ProgressBar {
                id: modLevel
                Layout.fillWidth: true
                from: 0
                to: 255
                value: 0
            }
        }

        // Charts in a row
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // UDP Time Difference Chart
            SimpleLinePlot {
                id: timeDifferenceChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "UDP time difference"
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
                    titleText: "ms"
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
                title: "Waterfall plot time"
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
                    titleText: "ms"
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
                title: "Spectrum plot time"
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
                    titleText: "ms"
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
            text: "Cancel"
            Layout.alignment: Qt.AlignRight
            onClicked: {
                if (selectRadioController) {
                    selectRadioController.visible = false
                }
            }
        }
    }
}
