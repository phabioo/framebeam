import QtQuick
import FrameBeam.Player

// Status pill (tokens.md): surface + text with a leading symbol. tone: ok | warn | error | neutral; in the Player
// "warn" is the accent (Signal Cyan on its chip surface), by design. small = the diagnostics variant (11/600, no symbol).
// The symbol is left out when the text already starts with one.
Rectangle {
    id: root
    property string tone: "neutral"
    property string text: ""
    property bool small: false
    readonly property bool textHasSymbol: /^[●▲✕⟳↓]/.test(text)
    property string symbol: (small || textHasSymbol) ? ""
                          : (tone === "ok" ? "●" : (tone === "warn" ? "▲" : (tone === "error" ? "✕" : "")))

    implicitHeight: small ? 20 : 24
    implicitWidth: row.implicitWidth + (small ? 16 : 18)
    radius: Theme.radiusPill
    color: tone === "neutral" ? Theme.neutralPillBg : Theme.toneBg(tone)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Text {
            visible: root.symbol !== ""
            text: root.symbol
            font.pixelSize: root.small ? 9 : 10
            anchors.verticalCenter: parent.verticalCenter
            color: root.tone === "neutral" ? Theme.textMuted : Theme.toneColor(root.tone)
        }
        Text {
            text: root.text
            font.pixelSize: root.small ? Theme.fontMono : Theme.fontMeta
            font.weight: Font.DemiBold
            color: root.tone === "neutral" ? Theme.textMuted : Theme.toneColor(root.tone)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
