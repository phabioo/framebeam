import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Save conflict on start (design 3d). Modal; "Keep both, decide later" is the default and focused action,
// Esc = decide later, Tab/Shift+Tab cycle through the actions, Enter/Space trigger the focused one.
Item {
    id: root
    required property PlayerController player
    readonly property var conflict: player.saveConflict
    readonly property bool shown: conflict.active === true

    visible: opacity > 0
    opacity: shown ? 1 : 0
    anchors.fill: parent
    z: 100
    Behavior on opacity { NumberAnimation { duration: Theme.durPage } }

    onShownChanged: if (shown) keepButton.forceActiveFocus()

    // Dimmed library behind the dialog; swallows clicks.
    Rectangle {
        anchors.fill: parent
        color: Theme.dark ? Qt.rgba(8 / 255, 9 / 255, 10 / 255, 0.8) : Qt.rgba(0, 0, 0, 0.6)
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    FocusScope {
        id: scope
        anchors.fill: parent
        focus: root.shown
        Keys.onEscapePressed: if (!root.conflict.busy) root.player.resolveSaveConflict("later")

        Rectangle {
            id: card
            objectName: "conflictDialog"
            anchors.centerIn: parent
            width: Math.min(760, parent.width - 48)
            implicitHeight: col.implicitHeight + 64
            height: Math.min(implicitHeight, parent.height - 32)
            radius: Theme.radius12
            color: Theme.bgPanel
            border.width: 1
            border.color: Theme.borderInput

            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 32
                spacing: 24

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Eyebrow {
                        text: qsTr("▲ Save conflict · %1").arg(root.conflict.gameTitle || "")
                        color: Theme.warn
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        text: qsTr("The Hub and this device have different saves")
                        font.pixelSize: Theme.fontDialog
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        text: qsTr("This device kept playing offline while another device secured a new checkpoint. Nothing is overwritten until you decide. Both saves are secured in the history beforehand.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width < 560 ? 1 : 2
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater {
                        model: [
                            { objectName: "hubSide", eyebrow: qsTr("On the Hub"), side: root.conflict.hub },
                            { objectName: "localSide", eyebrow: qsTr("On this device"), side: root.conflict.local }
                        ]
                        delegate: Rectangle {
                            id: sideCard
                            required property var modelData
                            readonly property var side: modelData.side || ({})
                            objectName: modelData.objectName
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: sideCol.implicitHeight + 32
                            radius: Theme.radius9
                            color: Theme.popupBg
                            border.width: 1
                            border.color: Theme.borderCard
                            ColumnLayout {
                                id: sideCol
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 6
                                Eyebrow { text: sideCard.modelData.eyebrow }
                                FbLabel {
                                    Layout.fillWidth: true
                                    text: sideCard.side.title || ""
                                    font.pixelSize: Theme.fontSection
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                FbLabel {
                                    Layout.fillWidth: true
                                    text: sideCard.side.when || ""
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                                FbMono {
                                    Layout.fillWidth: true
                                    text: sideCard.side.base || ""
                                    font.pixelSize: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "conflictError"
                    visible: (root.conflict.error || "") !== ""
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 24
                    radius: Theme.radius8
                    color: Theme.errorBg
                    FbLabel {
                        id: errText
                        anchors.fill: parent
                        anchors.margins: 12
                        text: root.conflict.error || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                        color: Theme.errorText
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        FbButton {
                            id: hubButton
                            objectName: "useHubButton"
                            Layout.preferredWidth: 340
                            implicitHeight: 46
                            text: qsTr("Use Hub version")
                            enabled: !root.conflict.busy
                            busyOnClick: true
                            activeFocusOnTab: true
                            KeyNavigation.tab: localButton
                            KeyNavigation.backtab: keepButton
                            onClicked: root.player.resolveSaveConflict("use_hub")
                            Keys.onReturnPressed: clicked()
                            Keys.onEnterPressed: clicked()
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            text: qsTr("local save stays secured")
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        FbButton {
                            id: localButton
                            objectName: "useLocalButton"
                            Layout.preferredWidth: 340
                            implicitHeight: 46
                            text: qsTr("Adopt local save as new current version")
                            enabled: !root.conflict.busy
                            busyOnClick: true
                            activeFocusOnTab: true
                            KeyNavigation.tab: keepButton
                            KeyNavigation.backtab: hubButton
                            onClicked: root.player.resolveSaveConflict("use_local")
                            Keys.onReturnPressed: clicked()
                            Keys.onEnterPressed: clicked()
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            text: qsTr("becomes the new checkpoint")
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        FbButton {
                            id: keepButton
                            objectName: "keepBothButton"
                            Layout.preferredWidth: 340
                            implicitHeight: 46
                            kind: "primary"
                            text: qsTr("Keep both, decide later")
                            enabled: !root.conflict.busy
                            busyOnClick: true
                            activeFocusOnTab: true
                            KeyNavigation.tab: hubButton
                            KeyNavigation.backtab: localButton
                            onClicked: root.player.resolveSaveConflict("later")
                            Keys.onReturnPressed: clicked()
                            Keys.onEnterPressed: clicked()
                            // Visible focus ring also for the primary button
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: 11
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.text
                                visible: keepButton.activeFocus
                            }
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            text: qsTr("default · the game starts with the local save")
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("Decide later: the conflict stays visible on the game and on the Saves page in the Hub.")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
