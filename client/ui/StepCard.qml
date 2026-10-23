import QtQuick
import QtQuick.Layouts

// Step card in 3b; stage: "pending" | "active" | "done"
Rectangle {
    id: card
    property string title
    property string stage: "pending"
    default property alias content: body.data

    implicitHeight: row.implicitHeight + 32
    radius: 0
    color: Theme.surface
    border.width: 1
    border.color: stage === "active" ? Theme.accent : Theme.rule
    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        anchors.leftMargin: 18
        spacing: 12

        Item {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 20
            Layout.alignment: Qt.AlignTop
            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 0
                color: card.stage === "done" ? Theme.accent : "transparent"
                border.width: card.stage === "done" ? 0 : 1.5
                border.color: card.stage === "active" ? Theme.accent : Theme.textDisabled
            }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 8
            UzLabel {
                Layout.fillWidth: true
                text: card.title
                font.pixelSize: Theme.fontSection
                font.weight: Font.DemiBold
                color: card.stage === "pending" ? Theme.textFaint : Theme.text
            }
        }
    }
}
