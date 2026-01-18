import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Window {
    id: selectRadioDialog
    title: "Select Radio"
    width: 800
    height: 600

    signal selectedRadio(int radio)

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // Radio selection table
        TableView {
            id: radioTable
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 200

            clip: true
            boundsBehavior: Flickable.StopAtBounds

            model: radioTableModel

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
                        if (model.busy !== 1) {
                            selectRadioDialog.selectedRadio(model.row)
                            selectRadioDialog.close()
                        }
                    }
                }
            }

            // Column headers
            Row {
                id: header
                width: parent.width
                height: 25

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
        }

        // Audio level meters
        RowLayout {
            Layout.fillWidth: true
            spacing: 20

            Label {
                text: "AF Level:"
                color: "white"
            }

            ProgressBar {
                id: afLevel
                Layout.fillWidth: true
                from: 0
                to: 65535
                value: 0
            }

            Label {
                text: "Mod Level:"
                color: "white"
            }

            ProgressBar {
                id: modLevel
                Layout.fillWidth: true
                from: 0
                to: 65535
                value: 0
            }
        }

        // Charts in a row
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // UDP Time Difference Chart
            ChartView {
                id: timeDifferenceChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width / 3

                title: "UDP time difference"
                titleFont.bold: true
                titleFont.pointSize: 8

                legend.visible: false
                antialiasing: true
                backgroundColor: "#1e1e1e"

                ValueAxis {
                    id: timeXAxis
                    min: 0
                    max: 100
                    labelsVisible: false
                }

                ValueAxis {
                    id: timeYAxis
                    min: -10
                    max: 10
                    titleText: "ms"
                    titleFont.pointSize: 8
                    labelsColor: "white"
                    gridLineColor: "#404040"
                }

                LineSeries {
                    id: timeDifferenceSeries
                    axisX: timeXAxis
                    axisY: timeYAxis
                    color: "#2196F3"
                    width: 2
                }
            }

            // Waterfall Plot Time Chart
            ChartView {
                id: waterfallChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width / 3

                title: "Waterfall plot time"
                titleFont.bold: true
                titleFont.pointSize: 8

                legend.visible: false
                antialiasing: true
                backgroundColor: "#1e1e1e"

                ValueAxis {
                    id: waterfallXAxis
                    min: 0
                    max: 1000
                    labelsVisible: false
                }

                ValueAxis {
                    id: waterfallYAxis
                    min: -10
                    max: 10
                    titleText: "ms"
                    titleFont.pointSize: 8
                    labelsColor: "white"
                    gridLineColor: "#404040"
                }

                LineSeries {
                    id: waterfallSeries
                    axisX: waterfallXAxis
                    axisY: waterfallYAxis
                    color: "#4CAF50"
                    width: 2
                }
            }

            // Spectrum Plot Time Chart
            ChartView {
                id: spectrumChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width / 3

                title: "Spectrum plot time"
                titleFont.bold: true
                titleFont.pointSize: 8

                legend.visible: false
                antialiasing: true
                backgroundColor: "#1e1e1e"

                ValueAxis {
                    id: spectrumXAxis
                    min: 0
                    max: 1000
                    labelsVisible: false
                }

                ValueAxis {
                    id: spectrumYAxis
                    min: 0
                    max: 50
                    titleText: "ms"
                    titleFont.pointSize: 8
                    labelsColor: "white"
                    gridLineColor: "#404040"
                }

                LineSeries {
                    id: spectrumSeries
                    axisX: spectrumXAxis
                    axisY: spectrumYAxis
                    color: "#FF9800"
                    width: 2
                }
            }
        }

        // Cancel button
        Button {
            id: cancelButton
            text: "Cancel"
            Layout.alignment: Qt.AlignRight
            onClicked: selectRadioDialog.close()
        }
    }

    // Functions callable from C++
    function setAudioOutputLevel(level) {
        afLevel.value = level
    }

    function setAudioInputLevel(level) {
        modLevel.value = level
    }

    function addTimeDifferencePoint(counter, time) {
        timeDifferenceSeries.append(counter, time)

        if (timeDifferenceSeries.count > 100) {
            timeDifferenceSeries.remove(0)
            timeXAxis.min = counter - 100
            timeXAxis.max = counter
        }

        // Auto-scale Y axis
        var minY = 10, maxY = -10
        for (var i = 0; i < timeDifferenceSeries.count; i++) {
            var point = timeDifferenceSeries.at(i)
            if (point.y < minY) minY = point.y
            if (point.y > maxY) maxY = point.y
        }
        timeYAxis.min = Math.floor(minY - 1)
        timeYAxis.max = Math.ceil(maxY + 1)
    }

    function addWaterfallPoint(counter, time) {
        waterfallSeries.append(counter, time)

        if (waterfallSeries.count > 1000) {
            waterfallSeries.remove(0)
            waterfallXAxis.min = counter - 1000
            waterfallXAxis.max = counter
        }

        if (selectRadioDialog.visible) {
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

    function addSpectrumPoint(counter, time) {
        spectrumSeries.append(counter, time)

        if (spectrumSeries.count > 1000) {
            spectrumSeries.remove(0)
            spectrumXAxis.min = counter - 1000
            spectrumXAxis.max = counter
        }

        if (selectRadioDialog.visible) {
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
