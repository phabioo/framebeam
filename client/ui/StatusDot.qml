import QtQuick

// Statuspunkt; tone: ok | warn | error | neutral
Rectangle {
    property string tone: "neutral"
    implicitWidth: 8
    implicitHeight: 8
    radius: width / 2
    color: tone === "neutral" ? Theme.textDisabled : Theme.toneColor(tone)
}
