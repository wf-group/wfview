import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Titled frame whose body can be collapsed.
//
// Content children are placed in a ColumnLayout, so size them with Layout.*
// attached properties rather than anchors. Collapsing hides the body, which
// removes it from the layout entirely; the section's implicit size shrinks
// and anything sized from it (e.g. the enclosing window) can follow.
//
// The section's implicit size is bound directly to its inner layout rather
// than derived through a Frame/Pane, so an enclosing layout always sees the
// current size.
//
// headerItems sits to the right of the title and stays visible while
// collapsed, which suits an "Enable" switch or a small action button.
Item {
    id: root

    property string title: ""
    property bool expanded: true
    default property alias content: body.data
    property alias headerItems: headerExtras.data
    property alias bodySpacing: body.spacing

    Layout.fillWidth: true
    implicitWidth: sectionLayout.implicitWidth + 2
    implicitHeight: sectionLayout.implicitHeight + 2

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 2
        border.width: 1
        border.color: root.palette ? root.palette.mid : "#808080"
    }

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ToolButton {
                id: header
                Layout.fillWidth: true
                topPadding: 3
                bottomPadding: 3
                text: (root.expanded ? "▾  " : "▸  ") + root.title
                font.bold: true
                onClicked: root.expanded = !root.expanded

                contentItem: Label {
                    text: header.text
                    font: header.font
                    color: header.palette.buttonText
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 2
                }
            }

            RowLayout {
                id: headerExtras
                Layout.rightMargin: 6
                spacing: 6
            }
        }

        ColumnLayout {
            id: body
            visible: root.expanded
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            spacing: 2
        }
    }
}
