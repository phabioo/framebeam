import QtQuick
import QtQuick.Layouts

// Item of a second column (3e/3f/3p): a text item, square, with the 2 px Yamabuki start marker when selected (frame.md "0-4 E").
Rectangle {
    id: root
    property bool selected: false
    default property alias content: inner.data
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + 22
    radius: 0
    color: hover.hovered ? Theme.hover : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Rectangle {   // marker-start
        objectName: "columnItemMarker"
        visible: root.selected
        width: 2
        height: parent.height
        color: Theme.accent
    }
    ColumnLayout {
        id: inner
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        anchors.topMargin: 11
        anchors.bottomMargin: 11
        spacing: 3
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
