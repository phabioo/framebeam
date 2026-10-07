import QtQuick
import QtQuick.Layouts

// Item of a second column (3e/3f/3p): radius 8, padding 11 12; selected = raised surface with a 1 px ring.
Rectangle {
    id: root
    property bool selected: false
    default property alias content: inner.data
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + 22
    radius: Theme.radius8
    color: selected ? Theme.surfaceRaised : (hover.hovered ? Theme.surface : "transparent")
    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    // Ring 0 0 0 1px #2f3035
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Theme.borderPopup
        opacity: root.selected ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }
    ColumnLayout {
        id: inner
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 11
        anchors.bottomMargin: 11
        spacing: 3
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
