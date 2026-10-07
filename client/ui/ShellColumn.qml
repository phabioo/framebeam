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

    color: Theme.bgPanel
    implicitWidth: Theme.columnWidth

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSidebar
    }
    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.leftMargin: Theme.space16
        anchors.rightMargin: Theme.space16
        anchors.topMargin: Theme.space28
        anchors.bottomMargin: Theme.space20
        spacing: Theme.space6

        Eyebrow {
            visible: root.title !== ""
            text: root.title
            Layout.leftMargin: 4
            Layout.bottomMargin: Theme.space6
        }
    }
}
