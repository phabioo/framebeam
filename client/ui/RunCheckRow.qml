import QtQuick
import QtQuick.Layouts
import Uzume.App

// One selectable row of the run dialogs (plugin, player or game): a tick box (or radio dot) with a title and a sub line.
Rectangle {
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
    implicitHeight: col.implicitHeight + 16
    radius: Theme.radius7
    color: root.checked ? Theme.accentChipBg : (hover.hovered ? Theme.rowHover : "transparent")
    border.width: root.checked ? 1 : 0
    border.color: Theme.accentBoxBorder
    Accessible.role: root.radio ? Accessible.RadioButton : Accessible.CheckBox
    Accessible.name: root.title
    Accessible.checked: root.checked

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            radius: 0
            color: root.checked ? Theme.accent : "transparent"
            border.width: 1.5
            border.color: root.checked ? Theme.accent : Theme.borderButton
            Text {
                anchors.centerIn: parent
                visible: root.checked && !root.radio
                text: "✓"
                font.pixelSize: 11
                font.weight: Font.Bold
                color: Theme.textOnAccent
            }
        }
        ColumnLayout {
            id: col
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                UzLabel {
                    Layout.fillWidth: true
                    text: root.title
                    elide: Text.ElideRight
                    font.weight: Font.Medium
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
                color: Theme.textMuted
                font.pixelSize: Theme.fontMeta
            }
            UzLabel {
                Layout.fillWidth: true
                visible: root.detail !== ""
                text: root.detail
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontMeta
            }
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.toggled() }
}
