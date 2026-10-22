import QtQuick
import QtQuick.Layouts
import Uzume.App

// Warning box for plugins Uzume has not checked (same tone as turning on an unverified plugin in Settings > Plugins).
Rectangle {
    id: root
    property string text: ""
    Layout.fillWidth: true
    implicitHeight: label.implicitHeight + 20
    radius: Theme.radius7
    color: Theme.pluginWarnBg
    border.width: 1
    border.color: Theme.pluginWarnLine
    FbLabel {
        id: label
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        text: root.text
        color: Theme.pluginWarnText
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WordWrap
    }
}
