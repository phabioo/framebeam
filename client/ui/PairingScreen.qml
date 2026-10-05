import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameBeam.Player

// 3b: Hub hinzufuegen (Hub erkannt, Zertifikat bestaetigen, Geraet freigeben).
Rectangle {
    id: root
    required property PlayerController player
    readonly property var info: player.pairing
    readonly property string phase: info.phase
    color: Theme.bg

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: column
            width: Math.min(640, flick.width - 32)
            x: (flick.width - width) / 2
            y: Math.max(32, (flick.height - implicitHeight) / 2)
            spacing: 14

            ColumnLayout {
                spacing: 6
                Layout.bottomMargin: 8
                FbLabel {
                    text: qsTr("Hub hinzufügen")
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.6
                }
                FbMono { text: root.info.address; color: Theme.textMuted; font.pixelSize: 13 }
            }

            // Schritt 1: Hub erkannt
            StepCard {
                Layout.fillWidth: true
                objectName: "stepHub"
                title: root.info.hubKnown ? qsTr("Hub erkannt") : qsTr("Hub wird gesucht…")
                stage: root.info.hubKnown ? "done" : "active"
                FbMono {
                    visible: root.info.hubKnown
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    text: qsTr("%1 · FrameBeam Hub %2 · Protokoll v%3 · kompatibel")
                              .arg(root.info.hubName).arg(root.info.hubVersion).arg(root.info.protocol)
                }
            }

            // Schritt 2: Zertifikat (TOFU, explizite Bestaetigung)
            StepCard {
                id: certStep
                Layout.fillWidth: true
                objectName: "stepCertificate"
                title: root.phase === "trust" ? qsTr("Zertifikat prüfen")
                       : (certStep.stage === "done" ? (root.info.tls ? qsTr("Zertifikat vertraut") : qsTr("Unverschlüsselte Verbindung")) : qsTr("Zertifikat"))
                stage: !root.info.hubKnown ? "pending" : (root.phase === "trust" ? "active" : "done")

                FbMono {
                    visible: root.info.tls && root.info.hubKnown
                    objectName: "fingerprintText"
                    Layout.fillWidth: true
                    text: root.info.fingerprint
                    color: Theme.text
                    font.pixelSize: 12
                    lineHeight: 1.35
                }
                FbLabel {
                    visible: root.phase === "trust"
                    Layout.fillWidth: true
                    text: qsTr("SHA-256-Fingerprint des Hub-Zertifikats. Vergleiche ihn mit der Anzeige im Hub und vertraue ihm nur, wenn beide übereinstimmen. Ändert sich das Zertifikat später, wird die Verbindung blockiert.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    visible: root.phase === "trust"
                    Layout.topMargin: 4
                    spacing: 10
                    FbButton {
                        objectName: "trustButton"
                        kind: "primary"
                        text: qsTr("Fingerprint vertrauen")
                        onClicked: root.player.confirmTrust()
                    }
                    FbButton {
                        objectName: "rejectButton"
                        text: qsTr("Abbrechen")
                        onClicked: root.player.rejectTrust()
                    }
                }
                FbLabel {
                    visible: root.phase !== "trust" && root.info.hubKnown
                    Layout.fillWidth: true
                    text: root.info.tls ? qsTr("Gespeichert. Ändert sich das Zertifikat später, wird die Verbindung blockiert.")
                                        : qsTr("Nur für die Entwicklung: Dieser Hub wird ohne TLS angesprochen, es gibt keinen Fingerprint.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            // Schritt 3: Geraet freigeben
            StepCard {
                id: approvalStep
                Layout.fillWidth: true
                objectName: "stepApproval"
                title: qsTr("Gerät freigeben")
                stage: (root.phase === "trust" || !root.info.hubKnown) ? "pending" : "active"

                RowLayout {
                    visible: approvalStep.stage === "active"
                    spacing: 0
                    Rectangle {
                        implicitWidth: requestSeg.implicitWidth + 24
                        implicitHeight: 32
                        radius: 5
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.borderInput
                        FbLabel { id: requestSeg; anchors.centerIn: parent; text: qsTr("Freigabe anfragen"); font.pixelSize: 13; font.weight: Font.Medium }
                    }
                    Rectangle {
                        objectName: "inviteSegment"
                        implicitWidth: inviteSeg.implicitWidth + 24
                        implicitHeight: 32
                        color: "transparent"
                        FbLabel {
                            id: inviteSeg
                            anchors.centerIn: parent
                            text: qsTr("Einladung einlösen · folgt")
                            font.pixelSize: 13
                            color: Theme.textDisabled
                        }
                    }
                }

                RowLayout {
                    visible: root.phase === "awaiting" || root.phase === "authenticating"
                    spacing: 12
                    BusyIndicator {
                        running: parent.visible
                        implicitWidth: 22
                        implicitHeight: 22
                        palette.dark: Theme.accent
                    }
                    ColumnLayout {
                        spacing: 2
                        FbLabel {
                            text: root.phase === "authenticating" ? qsTr("Anmelden…") : qsTr("Warte auf Freigabe durch den Admin")
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                        FbLabel {
                            visible: root.phase === "awaiting"
                            text: qsTr("Die Anfrage erscheint im Hub unter Clients.")
                            color: Theme.textMuted
                            font.pixelSize: 12
                        }
                    }
                }

                FbLabel {
                    visible: root.phase === "needsPairing"
                    Layout.fillWidth: true
                    text: qsTr("Dieses Gerät ist am Hub noch nicht freigegeben. Der Admin des Hubs muss die Anfrage bestätigen.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    visible: root.phase === "denied" || root.phase === "expired"
                    objectName: "pairingProblem"
                    Layout.fillWidth: true
                    implicitHeight: problemText.implicitHeight + 24
                    radius: 8
                    color: root.phase === "denied" ? Theme.errorBg : Theme.warnBg
                    FbLabel {
                        id: problemText
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        color: root.phase === "denied" ? Theme.errorText : Theme.text
                        text: root.phase === "denied" ? qsTr("Die Anfrage wurde vom Admin abgelehnt.")
                                                      : qsTr("Die Anfrage ist abgelaufen. Bitte erneut anfragen.")
                    }
                }
                FbLabel {
                    visible: root.info.error !== "" && approvalStep.stage === "active"
                    Layout.fillWidth: true
                    text: root.info.error
                    color: Theme.error
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    visible: approvalStep.stage === "active"
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.topMargin: 4
                    Eyebrow { text: qsTr("Gerätename"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.deviceName; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    Eyebrow { text: qsTr("Plattform"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.platform; color: Theme.text; Layout.fillWidth: true }
                    Eyebrow { text: qsTr("Player-Version"); Layout.preferredWidth: 130 }
                    FbMono { text: root.info.playerVersion; color: Theme.text; Layout.fillWidth: true }
                }

                RowLayout {
                    visible: approvalStep.stage === "active"
                    Layout.topMargin: 4
                    spacing: 10
                    FbButton {
                        objectName: "requestButton"
                        visible: root.phase === "needsPairing" || root.phase === "denied" || root.phase === "expired"
                        kind: "primary"
                        text: root.phase === "needsPairing" ? qsTr("Freigabe anfragen") : qsTr("Neu anfragen")
                        onClicked: root.player.requestPairing()
                    }
                    FbButton {
                        objectName: "cancelRequestButton"
                        visible: root.phase === "awaiting"
                        text: qsTr("Anfrage abbrechen")
                        onClicked: root.player.cancelPairing()
                    }
                    FbButton {
                        objectName: "backButton"
                        visible: root.phase !== "awaiting"
                        text: qsTr("Zurück")
                        onClicked: root.player.leavePairing()
                    }
                }
            }
        }
    }
}
