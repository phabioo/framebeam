import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Second column of the shell (3e devices/systems, 3f devices, 3p sections): 300 px, panel surface, border right.
// Children go below the eyebrow title.
Rectangle {
    id: root
    property string title: ""
    default property alias content: col.data
    property alias spacing: col.spacing

    color: Theme.ground
    implicitWidth: Theme.columnWidth

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.rule
    }
    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        anchors.topMargin: Theme.space3
        anchors.bottomMargin: Theme.space2
        spacing: Theme.space1

        Eyebrow {
            visible: root.title !== ""
            text: root.title
            Layout.leftMargin: 4
            Layout.bottomMargin: Theme.space05
        }
    }
}
