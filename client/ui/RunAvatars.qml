import QtQuick
import Uzume.App

// Participants of a run as Avatars (design-system.md "Avatar": a square in the person colour with the initial). `participants` is
// a list of {name, color} with color 0..7 (-1 = none yet, shown in the sixth colour); the Repeater counts instead of iterating
// the JS array (Qt 6.4 handles arrays badly in nested layouts).
Row {
    id: root
    property var participants: []
    property int size: 20
    spacing: Theme.space05
    Repeater {
        model: root.participants.length
        delegate: UzAvatar {
            id: av
            required property int index
            readonly property var who: root.participants[av.index]
            size: root.size
            name: av.who.name
            person: (av.who.color >= 0 ? av.who.color : 5) + 1
        }
    }
}
