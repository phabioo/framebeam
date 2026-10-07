import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// 3c: "SESSIONS ON THIS HUB": Sessions of other Players (hidden when there are none). Live: the list follows the
// Hub link, cards appear and disappear while the Library is open.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var list: player.sessions.sessions
    objectName: "sessionsSection"
    visible: list.length > 0
    spacing: Theme.space10

    RowLayout {
        spacing: Theme.space8
        StatusDot { tone: "ok" }
        Eyebrow { text: qsTr("SESSIONS ON THIS HUB"); color: Theme.ok }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: root.width >= 620 ? 2 : 1
        columnSpacing: Theme.space12
        rowSpacing: Theme.space12
        Repeater {
            model: root.list
            delegate: Rectangle {
                id: card
                required property var modelData
                objectName: "sessionCard_" + modelData.sessionId
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: cardRow.implicitHeight + 24
                radius: Theme.radius10
                color: Theme.surface
                border.width: 1
                border.color: card.modelData.invited ? Theme.accent : Theme.borderCard
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity { NumberAnimation { duration: Theme.durList } }

                RowLayout {
                    id: cardRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: Theme.space12
                    Rectangle {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        Layout.alignment: Qt.AlignTop
                        radius: 15
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.borderInput
                        Text {
                            anchors.centerIn: parent
                            text: (card.modelData.title || "?").charAt(0).toUpperCase()
                            font.pixelSize: Theme.fontMeta
                            font.weight: Font.DemiBold
                            color: Theme.textSecondary
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        FbLabel {
                            objectName: "sessionTitle"
                            Layout.fillWidth: true
                            text: card.modelData.title
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        FbLabel {
                            objectName: "sessionMeta"
                            Layout.fillWidth: true
                            text: card.modelData.meta
                            color: card.modelData.invited ? Theme.accent : Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.topMargin: 6
                            spacing: Theme.space8
                            FbButton {
                                objectName: (card.modelData.invited ? "join_" : "watch_") + card.modelData.sessionId
                                implicitHeight: 32
                                kind: card.modelData.invited ? "primary" : "outline"
                                font.pixelSize: Theme.fontSmall
                                busy: root.player.sessions.joining
                                text: card.modelData.invited ? qsTr("Join") : qsTr("Watch Session")
                                enabled: root.player.sessions.hubLink === "online" && !root.player.sessions.joining
                                onClicked: root.player.sessions.watch(card.modelData.sessionId)
                            }
                            FbButton {
                                visible: card.modelData.invited
                                objectName: "decline_" + card.modelData.sessionId
                                implicitHeight: 32
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("Decline")
                                onClicked: root.player.sessions.decline(card.modelData.sessionId)
                            }
                        }
                    }
                }
            }
        }
    }
}
