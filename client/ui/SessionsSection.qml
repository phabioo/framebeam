import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// 3c: "SESSIONS ON THIS HUB": Sessions of other Players (hidden when there are none).
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var list: player.sessions.sessions
    objectName: "sessionsSection"
    visible: list.length > 0
    spacing: 10

    Eyebrow { text: qsTr("SESSIONS ON THIS HUB") }

    Flow {
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: root.list
            delegate: Rectangle {
                id: card
                required property var modelData
                objectName: "sessionCard_" + modelData.sessionId
                width: Math.min(360, Math.max(260, (root.width - 10) / 2))
                implicitHeight: cardCol.implicitHeight + 24
                radius: 8
                color: Theme.surface
                border.width: 1
                border.color: card.modelData.invited ? Theme.accent : Theme.borderCard

                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    FbLabel {
                        objectName: "sessionTitle"
                        Layout.fillWidth: true
                        text: card.modelData.title
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    FbLabel {
                        objectName: "sessionMeta"
                        Layout.fillWidth: true
                        text: card.modelData.meta
                        color: card.modelData.invited ? Theme.accent : Theme.textMuted
                        font.pixelSize: 12
                    }
                    RowLayout {
                        spacing: 8
                        FbButton {
                            objectName: (card.modelData.invited ? "join_" : "watch_") + card.modelData.sessionId
                            implicitHeight: 32
                            kind: "primary"
                            font.pixelSize: 13
                            text: card.modelData.invited ? qsTr("Join") : qsTr("Watch Session")
                            enabled: root.player.sessions.hubLink === "online" && !root.player.sessions.joining
                            onClicked: root.player.sessions.watch(card.modelData.sessionId)
                        }
                        FbButton {
                            visible: card.modelData.invited
                            objectName: "decline_" + card.modelData.sessionId
                            implicitHeight: 32
                            font.pixelSize: 13
                            text: qsTr("Decline")
                            onClicked: root.player.sessions.decline(card.modelData.sessionId)
                        }
                    }
                }
            }
        }
    }
}
