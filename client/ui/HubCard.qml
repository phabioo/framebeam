import QtQuick
import QtQuick.Layouts

// Karte eines gespeicherten (oder gerade versuchten) Hubs in 3a.
Rectangle {
    id: card
    required property var hub

    signal connectRequested(string hubId)
    signal retryRequested()
    signal removeRequested(string hubId)

    property bool showFingerprints: false
    readonly property string status: hub.status
    readonly property string tone: hub.tone

    implicitHeight: body.implicitHeight + 32
    radius: 10
    color: Theme.surface
    border.width: hub.isLast && status === "idle" ? 1.5 : 1
    border.color: hub.isLast && status === "idle" ? Theme.accent : (status === "certChanged" ? Theme.error : Theme.borderCard)

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
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            StatusDot { tone: card.tone }
            FbLabel {
                text: card.hub.statusText
                font.pixelSize: 13
                color: card.tone === "neutral" ? Theme.textMuted : Theme.toneColor(card.tone)
            }
        }

        FbMono {
            Layout.fillWidth: true
            text: card.hub.detail
            elide: Text.ElideRight
        }

        // Hinweis bei Fehler/Warnung (Zertifikat geaendert, Hub/Player zu alt, nicht erreichbar)
        Rectangle {
            Layout.fillWidth: true
            visible: card.hub.message !== ""
            implicitHeight: msg.implicitHeight + 24
            radius: 8
            color: Theme.toneBg(card.tone)
            FbLabel {
                id: msg
                anchors.fill: parent
                anchors.margins: 12
                text: card.hub.message
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                color: card.tone === "error" ? Theme.errorText : Theme.text
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: card.status === "certChanged" && card.showFingerprints
            spacing: 6
            Eyebrow { text: qsTr("Gespeichert (SHA-256)") }
            FbMono { text: card.hub.expectedFingerprint || ""; color: Theme.text }
            Eyebrow { text: qsTr("Jetzt vom Hub gemeldet (SHA-256)"); Layout.topMargin: 4 }
            FbMono { text: card.hub.observedFingerprint || ""; color: Theme.error }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            FbButton {
                visible: card.status === "idle"
                kind: "primary"
                text: qsTr("Verbinden")
                onClicked: card.connectRequested(card.hub.hubId)
            }
            FbButton {
                visible: card.status === "unreachable"
                text: qsTr("Wiederholen")
                onClicked: card.retryRequested()
            }
            FbButton {
                visible: card.status === "certChanged"
                kind: "link"
                text: card.showFingerprints ? qsTr("Fingerprints ausblenden") : qsTr("Fingerprint prüfen")
                onClicked: card.showFingerprints = !card.showFingerprints
            }
            Item { Layout.fillWidth: true }
            FbButton {
                visible: card.status !== "connecting"
                kind: "link"
                text: card.hub.saved ? qsTr("Entfernen") : qsTr("Verwerfen")
                onClicked: card.removeRequested(card.hub.hubId)
            }
        }
    }
}
