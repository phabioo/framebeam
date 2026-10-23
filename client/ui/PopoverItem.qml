import QtQuick
import QtQuick.Layouts
import Uzume.App

// Row of an anchored menu (plugin console and plugin surfaces): 32 high, hover `hover`. glyph/icon left, text, trailing text or check.
Rectangle {
    id: root
    property string text: ""
    property string glyph: ""
    property string iconKind: ""
    property string trailing: ""
    property bool current: false
    property bool divider: false      // top border (separates a group)
    property bool monoText: false
    signal activated()

    implicitHeight: 32 + (divider ? 3 : 0)
    radius: 0
    color: "transparent"
    Accessible.role: Accessible.MenuItem
    Accessible.name: text

    Rectangle { visible: root.divider; anchors.top: parent.top; anchors.topMargin: 1; width: parent.width; height: 1; color: Theme.rule2 }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.divider ? 3 : 0
        radius: 0
        color: hover.hovered ? Theme.hover : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            Item {
                visible: root.glyph !== "" || root.iconKind !== ""
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Text { visible: root.glyph !== ""; anchors.centerIn: parent; text: root.glyph; font.pixelSize: 14; color: Theme.ink }
                GameIcon { visible: root.iconKind !== ""; anchors.centerIn: parent; kind: root.iconKind; color: root.current ? Theme.ink : Theme.muted }
            }
            UzLabel {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: root.text
                font.pixelSize: Theme.roles.buttonSm.size
                font.family: root.monoText ? Theme.mono : Qt.application.font.family
                font.weight: root.current ? Font.Medium : Font.Normal
                color: root.current ? Theme.ink : Theme.ink2
            }
            UzLabel {
                visible: root.trailing !== "" || root.current
                text: root.trailing !== "" ? root.trailing : "✓"
                font.pixelSize: 10
                font.family: Theme.mono
                color: root.trailing !== "" ? Theme.dim : Theme.accentText
            }
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.activated() }
    }
}
