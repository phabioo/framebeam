import QtQuick
import QtQuick.Layouts

// Card for a saved (or currently attempted) hub in 3a.
Rectangle {
    id: card
    required property var hub

    signal connectRequested(string hubId)
    signal retryRequested()
    signal removeRequested(string hubId)
    signal trustCertificateRequested(string fingerprint)
    signal cancelCertificateRequested()

    property bool confirmingTrust: false
    onStatusChanged: if (status !== "certChanged") confirmingTrust = false
    readonly property string status: hub.status
    readonly property string tone: hub.tone

    implicitHeight: body.implicitHeight + 32
    radius: Theme.radius10
    color: Theme.surface
    border.width: hub.isLast && status === "idle" ? 1.5 : 1
    border.color: hub.isLast && status === "idle" ? Theme.accent : (status === "certChanged" ? Theme.error : Theme.borderCard)
    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 16
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            FbLabel {
                Layout.fillWidth: true
                text: card.hub.name
                font.pixelSize: Theme.fontCard
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            FbPill {
                objectName: "hubStatusPill"
                tone: card.status === "idle" ? "ok" : card.tone
                symbol: card.status === "connecting" ? "…" : (card.status === "idle" ? "●" : (card.tone === "warn" ? "▲" : (card.tone === "error" ? "✕" : "")))
                text: card.status === "idle" ? qsTr("Reachable") : card.hub.statusText
            }
        }

        FbMono {
            Layout.fillWidth: true
            text: card.hub.detail
            elide: Text.ElideRight
        }

        // Notice on error/warning (certificate changed, hub/player too old, not reachable)
        Rectangle {
            Layout.fillWidth: true
            visible: card.hub.message !== ""
            implicitHeight: msg.implicitHeight + 24
            radius: Theme.radius8
            color: card.tone === "warn" ? Theme.infoBg : Theme.toneBg(card.tone)
            border.width: card.tone === "warn" ? 1 : 0
            border.color: Theme.infoBorder
            FbLabel {
                id: msg
                anchors.fill: parent
                anchors.margins: 12
                text: card.hub.message
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                color: card.tone === "error" ? Theme.errorText : (card.tone === "warn" ? Theme.infoText : Theme.text)
            }
        }

        GridLayout {
            objectName: "certFingerprints"
            Layout.fillWidth: true
            visible: card.status === "certChanged"
            columns: width > 640 ? 2 : 1
            columnSpacing: 20
            rowSpacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 4
                Eyebrow { text: qsTr("Saved (SHA-256)") }
                FbMono { objectName: "certExpectedFingerprint"; Layout.fillWidth: true; text: card.hub.expectedFingerprint || ""; color: Theme.text; wrapMode: Text.WrapAnywhere }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 4
                Eyebrow { text: qsTr("Presented by the Hub now (SHA-256)") }
                FbMono { objectName: "certObservedFingerprint"; Layout.fillWidth: true; text: card.hub.observedFingerprint || ""; color: Theme.error; wrapMode: Text.WrapAnywhere }
            }
        }

        FbLabel {
            objectName: "certHint"
            Layout.fillWidth: true
            visible: card.status === "certChanged"
            text: qsTr("Open the Hub's web Settings page and compare its certificate fingerprint with the one presented now. "
                       + "A Hub renews its certificate automatically before it expires.")
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        Rectangle {
            objectName: "certConfirmBox"
            Layout.fillWidth: true
            visible: card.status === "certChanged" && card.confirmingTrust
            implicitHeight: confirmCol.implicitHeight + 24
            radius: 8
            color: Theme.toneBg("error")
            ColumnLayout {
                id: confirmCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("Trust this certificate only if both fingerprints match the Hub's Settings page. "
                               + "Your saved sign-in is kept and used for the new certificate.")
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    color: Theme.errorText
                }
                RowLayout {
                    spacing: 10
                    FbButton {
                        objectName: "certConfirmTrust"
                        kind: "primary"
                        text: qsTr("Yes, trust this certificate")
                        onClicked: {
                            card.confirmingTrust = false
                            card.trustCertificateRequested(card.hub.observedFingerprintRaw)
                        }
                    }
                    FbButton {
                        objectName: "certConfirmBack"
                        text: qsTr("Back")
                        onClicked: card.confirmingTrust = false
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            FbButton {
                objectName: "hubConnectButton"
                visible: card.status === "idle" || card.status === "connecting"
                kind: "primary"
                busy: card.status === "connecting"
                busyOnClick: true
                enabled: card.status === "idle"
                text: card.status === "connecting" ? qsTr("Connecting…") : qsTr("Connect")
                onClicked: card.connectRequested(card.hub.hubId)
            }
            FbButton {
                visible: card.status === "unreachable" || card.status === "userDisabled"
                busyOnClick: true
                text: qsTr("Retry")
                onClicked: card.retryRequested()
            }
            FbButton {
                objectName: "certTrustNew"
                visible: card.status === "certChanged" && !card.confirmingTrust
                text: qsTr("Trust new certificate")
                onClicked: card.confirmingTrust = true
            }
            FbButton {
                objectName: "certCancel"
                visible: card.status === "certChanged"
                text: qsTr("Cancel")
                onClicked: card.cancelCertificateRequested()
            }
            Item { Layout.fillWidth: true }
            FbButton {
                visible: card.status !== "connecting"
                kind: "link"
                text: card.hub.saved ? qsTr("Remove") : qsTr("Discard")
                onClicked: card.removeRequested(card.hub.hubId)
            }
        }
    }
}
