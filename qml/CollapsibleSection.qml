import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Frame {
    id: root

    property string title: ""
    property bool expanded: true
    default property alias content: contentItem.data

    Layout.fillWidth: true
    Layout.minimumWidth: contentItem.contentWidth + 16
    padding: 0
    implicitWidth: Math.max(header.implicitWidth, contentItem.contentWidth + 16)
    implicitHeight: sectionLayout.implicitHeight

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        spacing: 0

        ToolButton {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            text: (root.expanded ? "v  " : ">  ") + root.title
            font.bold: true
            display: AbstractButton.TextOnly
            onClicked: root.expanded = !root.expanded

            contentItem: Label {
                text: header.text
                font: header.font
                color: header.palette.buttonText
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
                rightPadding: 8
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? contentItem.contentHeight + 16 : 0
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }

            Item {
                id: contentItem
                x: 8
                y: 8
                width: Math.max(contentWidth, body.width - 16)
                height: Math.max(0, body.height - 16)

                readonly property real contentWidth: {
                    var w = 0
                    for (var i = 0; i < children.length; ++i) {
                        var child = children[i]
                        w = Math.max(w, child.implicitWidth > 0 ? child.implicitWidth : child.width)
                    }
                    return w
                }

                readonly property real contentHeight: {
                    var h = 0
                    for (var i = 0; i < children.length; ++i) {
                        var child = children[i]
                        h = Math.max(h, child.implicitHeight > 0 ? child.implicitHeight : child.height)
                    }
                    return h
                }
            }
        }
    }
}
