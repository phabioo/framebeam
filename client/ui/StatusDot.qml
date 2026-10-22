import QtQuick

// Status mark: a 6 px square (never a disc). tone: ok | warn | error | neutral
Rectangle {
    property string tone: "neutral"
    implicitWidth: 6
    implicitHeight: 6
    radius: 0
    color: tone === "neutral" ? Theme.disabledInk : Theme.toneColor(tone)
}
