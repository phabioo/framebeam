import QtQuick
import QtQuick.Layouts
import Uzume.App

// Save conflict on start (design 3d). Modal; "Keep both, decide later" is the default and focused action,
// Esc = decide later, Tab/Shift+Tab cycle through the actions, Enter/Space trigger the focused one.
// A UzDialog (design-system.md "Dialog", size lg): label, title, text, the two sides, one action per outcome.
UzDialog {
    id: root
    required property PlayerController player
    readonly property var conflict: player.saveConflict

    size: "lg"
    busy: conflict.busy === true
    label: qsTr("Save conflict · %1").arg(conflict.gameTitle || "")
    title: qsTr("The server and this device have different saves")
    text: qsTr("This device kept playing offline while another device secured a new checkpoint. Nothing is overwritten until you decide. Both saves are secured in the history beforehand.")
    error: conflict.error || ""

    // Open while the controller has a conflict; Esc and the close button mean "decide later" (never while it is busy).
    Binding { target: root; property: "shown"; value: root.conflict.active === true }
    onClosed: root.player.resolveSaveConflict("later")
    // The frame keeps the name the tests look for.
    Component.onCompleted: frame.objectName = "conflictDialog"
    onShownChanged: if (shown) Qt.callLater(() => keepButton.forceActiveFocus())

    // The two sides next to each other (one under the other when narrow): open columns on a hairline, no cards.
    GridLayout {
        Layout.fillWidth: true
        columns: width < 440 ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space15
        Repeater {
            model: [
                { objectName: "hubSide", eyebrow: qsTr("On the server"), side: root.conflict.hub },
                { objectName: "localSide", eyebrow: qsTr("On this device"), side: root.conflict.local }
            ]
            delegate: Item {
                id: sideBlock
                required property var modelData
                readonly property var side: modelData.side || ({})
                objectName: modelData.objectName
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                implicitHeight: sideCol.implicitHeight + Theme.space1 + 1
                Rectangle { width: parent.width; height: 1; color: Theme.rule }
                ColumnLayout {
                    id: sideCol
                    y: Theme.space1 + 1
                    width: parent.width
                    spacing: Theme.space05
                    Eyebrow { Layout.fillWidth: true; text: sideBlock.modelData.eyebrow }
                    UzLabel {
                        Layout.fillWidth: true
                        text: sideBlock.side.title || ""
                        font.pixelSize: Theme.roles.itemTitle.size
                        font.weight: Theme.roles.itemTitle.weight
                        elide: Text.ElideRight
                    }
                    UzLabel {
                        Layout.fillWidth: true
                        text: sideBlock.side.when || ""
                        color: Theme.ink2
                        font.pixelSize: Theme.roles.input.size
                        elide: Text.ElideRight
                    }
                    UzMono {
                        Layout.fillWidth: true
                        text: sideBlock.side.base || ""
                        font.pixelSize: Theme.roles.meta11.size
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // One action per outcome, each with what it does underneath. Tab order: server, local, keep both.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space15
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzButton {
                id: hubButton
                objectName: "useHubButton"
                Layout.fillWidth: true
                text: qsTr("Use server version")
                enabled: !root.conflict.busy
                busyOnClick: true
                activeFocusOnTab: true
                KeyNavigation.tab: localButton
                KeyNavigation.backtab: keepButton
                onClicked: root.player.resolveSaveConflict("use_hub")
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }
            UzLabel {
                Layout.fillWidth: true
                text: qsTr("local save stays secured")
                color: Theme.muted
                font.pixelSize: Theme.roles.small.size
                wrapMode: Text.WordWrap
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzButton {
                id: localButton
                objectName: "useLocalButton"
                Layout.fillWidth: true
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
            UzLabel {
                Layout.fillWidth: true
                text: qsTr("becomes the new checkpoint")
                color: Theme.muted
                font.pixelSize: Theme.roles.small.size
                wrapMode: Text.WordWrap
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzButton {
                id: keepButton
                objectName: "keepBothButton"
                Layout.fillWidth: true
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
            }
            UzLabel {
                Layout.fillWidth: true
                text: qsTr("default · the game starts with the local save")
                color: Theme.muted
                font.pixelSize: Theme.roles.small.size
                wrapMode: Text.WordWrap
            }
        }
        UzLabel {
            Layout.fillWidth: true
            text: qsTr("Decide later: the conflict stays visible on the game and on the Saves page in the server.")
            color: Theme.dim
            font.pixelSize: Theme.roles.small.size
            wrapMode: Text.WordWrap
        }
    }
}
