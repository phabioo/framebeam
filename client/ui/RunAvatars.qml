import QtQuick
import FrameBeam.Player

// Participant avatars of a run row in the player colours (initial on a coloured disc). `participants` is a list of
// {name, color} with color 0..7 (-1 = none yet); the Repeater counts instead of iterating the JS array (Qt 6.4 handles
// arrays badly in nested layouts).
Row {
    id: root
    property var participants: []
    property int size: 24
    spacing: -6
    Repeater {
        model: root.participants.length
        delegate: Rectangle {
            id: av
            required property int index
            readonly property var person: root.participants[av.index]
            width: root.size
            height: root.size
            radius: root.size / 2
            color: Theme.playerColor((av.person.color >= 0 ? av.person.color : 5) + 1, Theme.dark)
            border.width: 2
            border.color: Theme.surface
            Text {
                anchors.centerIn: parent
                text: av.person.name.length > 0 ? av.person.name.charAt(0).toUpperCase() : "?"
                font.pixelSize: Math.round(root.size * 0.45)
                font.weight: Font.DemiBold
                color: Theme.dark ? "#121315" : "#ffffff"
            }
        }
    }
}
