import QtQuick
import QtQuick.Layouts
import Uzume.App

// One selectable row of the run dialogs (game, plugin or player): a hairline row on `rule-2` with a square box (radio: a dot
// inside the square) and a title with a sub line. The selected row has the `selected` tint and the 2 px Yamabuki start marker.
Item {
    id: root
    property string title: ""
    property string sub: ""
    property bool checked: false
    property bool radio: false
    property string pillOrigin: ""   // plugin rows: official | unverified | local, with its text
    property string pillText: ""
    property string detail: ""       // plugin rows: "It will be able to: ..."
    signal toggled()
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: Math.max(Theme.row44, col.implicitHeight + Theme.space15)
    Accessible.role: root.radio ? Accessible.RadioButton : Accessible.CheckBox
    Accessible.name: root.title
    Accessible.checked: root.checked

    Rectangle {
        anchors.fill: parent
        color: root.checked ? Theme.selected : (hover.hovered ? Theme.hover : "transparent")
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
    Rectangle { visible: root.checked; width: 2; height: parent.height; color: Theme.accent }   // marker-start

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space15
        anchors.rightMargin: Theme.space15
        spacing: Theme.space15
        Rectangle {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignVCenter
            radius: 0
            color: root.checked && !root.radio ? Theme.accent : "transparent"
            border.width: 1
            border.color: root.checked ? Theme.accent : Theme.muted
            Rectangle {
                visible: root.checked && root.radio
                anchors.centerIn: parent
                width: 8
                height: 8
                color: Theme.accent
            }
            UzGlyph {
                visible: root.checked && !root.radio
                anchors.centerIn: parent
                name: "check"
                size: 12
                strokeWidth: 1.8
                color: Theme.inkOnAccent
            }
        }
        ColumnLayout {
            id: col
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space1
                UzLabel {
                    Layout.fillWidth: true
                    text: root.title
                    elide: Text.ElideRight
                    font.weight: root.checked ? Font.Medium : Font.Normal
                }
                PluginOriginPill {
                    objectName: "runPluginOrigin"
                    visible: root.pillText !== ""
                    origin: root.pillOrigin
                    text: root.pillText
                }
            }
            UzLabel {
                Layout.fillWidth: true
                visible: root.sub !== ""
                text: root.sub
                elide: Text.ElideRight
                color: Theme.muted
                font.family: Theme.mono
                font.pixelSize: Theme.roles.meta11.size
            }
            UzLabel {
                Layout.fillWidth: true
                visible: root.detail !== ""
                text: root.detail
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: Theme.roles.small.size
            }
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.toggled() }
}
