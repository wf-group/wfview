// scope.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import RadioScope 1.0

SplitView {
    id: root
    anchors.fill: parent
    orientation: Qt.Vertical

	background: null
	
    handle: Rectangle {
        implicitHeight: 5
		width: parent.width
        color: "#202020"
    }
	
    SpectrumItem {
        id: spectrum
        objectName: "spectrum"
        SplitView.minimumHeight: 60
        SplitView.preferredHeight: root.height * 0.5
    }

    WaterfallItem {
        id: waterfall
        objectName: "waterfall"
        SplitView.minimumHeight: 60
        SplitView.preferredHeight: root.height * 0.5
        length: 128
    }
}
