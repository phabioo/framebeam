import QtQuick
import QtQuick.Layouts
import Uzume.App

// Note for plugins Uzume has not checked (same wording as turning on an unverified plugin in Settings > Plugins): a warning
// StatusIcon and a sentence, never a tinted banner (design-system.md "StatusIcon").
Item {
    id: root
    property string text: ""
    Layout.fillWidth: true
    implicitHeight: note.implicitHeight
    UzStatusIcon {
        id: note
        width: parent.width
        kind: "warning"
        text: root.text
        maxWidth: root.width
    }
}
