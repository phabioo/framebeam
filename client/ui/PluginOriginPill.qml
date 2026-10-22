import QtQuick
import Uzume.App

// Origin pill of a plugin (docs/design/plugins.md, common rules): "✓ Official" (info tone, signed by Uzume),
// "Hub upload · unverified" (warning tone, unsigned and offered by the Hub), "Local file" (neutral).
// origin: official | unverified | local; text comes from the controller (translated).
Rectangle {
    id: root
    property string origin: "local"
    property string text: ""
    readonly property bool official: origin === "official"
    readonly property bool unverified: origin === "unverified"
    // Official plugins carry no pill (round 3, sheet 0-2); the type stays for places that need the text (tests, tooltips).
    visible: !official

    implicitHeight: 20
    implicitWidth: label.implicitWidth + 16
    radius: Theme.radiusPill
    color: official ? Theme.infoBg : (unverified ? Theme.pluginWarnBg : Theme.neutralPillBg)
    border.width: official || unverified ? 1 : 0
    border.color: official ? Theme.infoBorder : Theme.pluginWarnLine

    Text {
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Theme.fontMono
        font.weight: Font.DemiBold
        color: root.official ? Theme.accent : (root.unverified ? Theme.pluginWarn : Theme.textMuted)
    }
}
