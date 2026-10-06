import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3g: side panel (340) of the game view: visibility, watchers/invites, share/stop, diagnostics.
Rectangle {
    id: root
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    property bool showDiagnostics: false
    objectName: "sessionPanel"
    color: Theme.gameHeader
    implicitWidth: 340

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.gameBorder }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: col
                width: flick.width
                spacing: 18

                Rectangle {
                    objectName: "panelHubBanner"
                    Layout.fillWidth: true
                    visible: root.ctl.hubLink !== "online"
                    implicitHeight: bannerText.implicitHeight + 16
                    radius: 8
                    color: Theme.warnBg
                    FbLabel {
                        id: bannerText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.warn
                        text: root.ctl.shared ? qsTr("Hub not reachable · reconnecting. The running Session continues until the Hub ends it.")
                                              : qsTr("Hub not reachable · reconnecting. Sharing needs a Hub connection.")
                    }
                }

                Rectangle {
                    objectName: "panelMessage"
                    Layout.fillWidth: true
                    visible: root.ctl.message !== ""
                    implicitHeight: msgText.implicitHeight + 16
                    radius: 8
                    color: root.ctl.messageIsError ? Theme.errorBg : Theme.surfaceRaised
                    FbLabel {
                        id: msgText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: root.ctl.messageIsError ? Theme.errorText : Theme.gameText
                        text: root.ctl.message
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    // Never wider than the panel: a header row whose labels need more room (no fonts, long
                    // translations) must elide instead of pushing the action buttons out of the viewport.
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: col.width
                    spacing: 8
                    Eyebrow { text: qsTr("Visibility") }
                    FbSegment {
                        objectName: "visibilitySegment"
                        Layout.fillWidth: true
                        stretch: true
                        current: root.ctl.visibility
                        options: [
                            { value: "private", label: qsTr("Private"), name: "visPrivate" },
                            { value: "hub_users", label: qsTr("Hub users"), name: "visHubUsers" },
                            { value: "invite_only", label: qsTr("Invite only"), name: "visInviteOnly" }
                        ]
                        onPicked: (v) => root.ctl.setVisibility(v)
                    }
                    FbLabel {
                        objectName: "visibilityHint"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        wrapMode: Text.WordWrap
                        text: qsTr("Your own devices can always watch.")
                        font.pixelSize: 11
                        color: Theme.gameTextMuted
                    }
                }

                // Watchers / invites (only while shared)
                ColumnLayout {
                    objectName: "participantsBlock"
                    Layout.fillWidth: true
                    // Never wider than the panel: a header row whose labels need more room (no fonts, long
                    // translations) must elide instead of pushing the action buttons out of the viewport.
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: col.width
                    visible: root.ctl.shared
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Eyebrow { objectName: "participantsTitle"; text: root.ctl.participantsTitle }
                        FbLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: qsTr("only you can change")
                            font.pixelSize: 11
                            color: Theme.gameTextMuted
                        }
                    }
                    FbLabel {
                        visible: root.ctl.participants.length === 0
                        text: root.ctl.visibility === "invite_only" ? qsTr("Nobody invited yet.") : qsTr("Nobody is watching yet.")
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                    }
                    Repeater {
                        model: root.ctl.participants
                        delegate: RowLayout {
                            id: prow
                            required property var modelData
                            objectName: "participant_" + modelData.id
                            Layout.fillWidth: true
                            spacing: 10
                            StatusDot { tone: prow.modelData.online ? "ok" : "neutral" }
                            ColumnLayout {
                                // The text column may shrink below its content so the action button always stays inside the panel.
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 0
                                spacing: 0
                                FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.name; font.pixelSize: 14; color: Theme.gameText }
                                FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; objectName: "participantStatus"; text: prow.modelData.status; font.pixelSize: 12; color: Theme.gameTextMuted }
                            }
                            FbButton {
                                objectName: (prow.modelData.kind === "viewer" ? "remove_" : "withdraw_") + prow.modelData.id
                                implicitHeight: 30
                                font.pixelSize: 12
                                focusPolicy: Qt.NoFocus
                                text: prow.modelData.action
                                onClicked: prow.modelData.kind === "viewer" ? root.ctl.removeViewer(prow.modelData.id)
                                                                           : root.ctl.withdrawInvite(prow.modelData.id)
                            }
                        }
                    }
                }

                // Invite search (invite_only)
                ColumnLayout {
                    objectName: "inviteBlock"
                    Layout.fillWidth: true
                    // Never wider than the panel: a header row whose labels need more room (no fonts, long
                    // translations) must elide instead of pushing the action buttons out of the viewport.
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: col.width
                    visible: root.ctl.visibility === "invite_only"
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        FbLabel { text: qsTr("Invite"); font.pixelSize: 12; color: Theme.gameTextMuted }
                        FbLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: qsTr("users of this Hub")
                            font.pixelSize: 11
                            color: Theme.gameTextMuted
                        }
                    }
                    FbField {
                        id: inviteField
                        objectName: "inviteField"
                        Layout.fillWidth: true
                        implicitHeight: 36
                        enabled: root.ctl.shared
                        font.family: Qt.application.font.family
                        placeholderText: root.ctl.shared ? qsTr("Search users…") : qsTr("Share the Session to invite users")
                        onTextChanged: root.ctl.searchUsers(text)
                    }
                    FbLabel {
                        objectName: "userSearchHint"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: root.ctl.userSearchHint.length > 0
                        wrapMode: Text.WordWrap
                        text: root.ctl.userSearchHint
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                    }
                    Repeater {
                        model: root.ctl.userResults
                        delegate: RowLayout {
                            id: urow
                            required property var modelData
                            objectName: "userResult_" + modelData.id
                            Layout.fillWidth: true
                            spacing: 10
                            StatusDot { tone: urow.modelData.online ? "ok" : "neutral" }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 0
                                spacing: 0
                                FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: urow.modelData.name; font.pixelSize: 14; color: Theme.gameText }
                                FbLabel {
                                    Layout.fillWidth: true
                                    text: urow.modelData.hint
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 11
                                    color: Theme.gameTextMuted
                                }
                            }
                            FbButton {
                                objectName: "invite_" + urow.modelData.id
                                implicitHeight: 30
                                kind: "primary"
                                font.pixelSize: 12
                                focusPolicy: Qt.NoFocus
                                text: qsTr("Invite")
                                onClicked: { root.ctl.invite(urow.modelData.id); inviteField.text = "" }
                            }
                        }
                    }
                }

                FbLabel {
                    objectName: "readOnlyNote"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.gameTextMuted
                    text: qsTr("Invitees only watch and listen. They send no input and cannot invite others.")
                }

                FbButton {
                    objectName: "diagToggle"
                    kind: "link"
                    focusPolicy: Qt.NoFocus
                    text: root.showDiagnostics ? qsTr("▾ Hide diagnostics") : qsTr("▸ Show diagnostics")
                    onClicked: root.showDiagnostics = !root.showDiagnostics
                }
                DiagnosticsPanel {
                    Layout.fillWidth: true
                    visible: root.showDiagnostics
                    player: root.player
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FbButton {
                objectName: "shareButton"
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                kind: root.ctl.shared ? "outline" : "primary"
                text: root.ctl.shared ? qsTr("Stop sharing") : (root.ctl.shareBusy ? qsTr("Sharing…") : qsTr("Share Session"))
                enabled: root.ctl.shared || (!root.ctl.shareBusy && root.ctl.available && root.ctl.hubLink === "online")
                onClicked: root.ctl.shared ? root.ctl.stopSharing() : root.ctl.shareSession()
            }
            FbButton {
                objectName: "endGameButton"
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                text: qsTr("End game and save")
                onClicked: root.player.quitGame()
            }
        }
    }
}
