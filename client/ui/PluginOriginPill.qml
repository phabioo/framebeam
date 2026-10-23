import QtQuick
import Uzume.App

// Origin tag of a plugin (design-system.md "Tag"): "Hub upload · unverified" (accent, unsigned and offered by the Hub),
// "Local file" (neutral), "✓ Official" (ok, signed by Uzume; official plugins carry no tag in the lists).
// origin: official | unverified | local; text comes from the controller (translated).
Item {
    id: root
    property string origin: "local"
    property string text: ""
    readonly property bool official: origin === "official"
    readonly property bool unverified: origin === "unverified"
    // Official plugins carry no pill (round 3, sheet 0-2); the type stays for places that need the text (tests, tooltips).
    visible: !official

    implicitHeight: tag.implicitHeight
    implicitWidth: tag.implicitWidth
    width: implicitWidth
    height: implicitHeight

    UzTag {
        id: tag
        text: root.text
        tone: root.official ? "ok" : (root.unverified ? "accent" : "neutral")
        icon: "none"
    }
}
