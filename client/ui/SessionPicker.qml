import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// "Running sessions" picker (3r-2, docs/design/player.md "Anchored popover"): content of the popover under "+ Add".
// Row per watchable session with "Add" or "✓ Added", the empty and full states and the footer note. It never opens on
// its own; the owner shows it in an AnchoredPopover.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    property real maxListHeight: 260
    readonly property var list: ctl.sessions
    readonly property var shown: ctl.shownSessionIds
    readonly property int count: ctl.surfaceCount
    readonly property bool full: count >= ctl.maxSurfaces
    objectName: "multiviewSessionList"
    spacing: 8

    function firstFree() {
        for (var i = 0; i < list.length; ++i) {
            if (shown.indexOf(list[i].sessionId) < 0) return list[i].title
        }
        return ""
    }

    RowLayout {
        Layout.fillWidth: true
        FbLabel {
            objectName: "multiviewListTitle"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            elide: Text.ElideRight
            text: qsTr("Running sessions")
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.gameText
        }
        FbMono { objectName: "multiviewListCount"; text: qsTr("%1 of %2 tiles").arg(root.count).arg(root.ctl.maxSurfaces); font.pixelSize: 11; color: Theme.gameMeta }
    }

    Rectangle {
        objectName: "multiviewNoSessions"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: root.list.length === 0
        implicitHeight: emptyCol.implicitHeight + 28
        radius: 8
        color: Theme.gameGroupBg
        border.width: 1
        border.color: Theme.gameDivider
        ColumnLayout {
            id: emptyCol
            anchors.fill: parent
            anchors.margins: 14
            anchors.topMargin: 14
            spacing: 4
            FbLabel { Layout.fillWidth: true; text: qsTr("No other sessions right now"); font.pixelSize: 13; color: Theme.gameBadgeText; wrapMode: Text.WordWrap }
            FbLabel {
                Layout.fillWidth: true
                text: qsTr("Sessions appear here when someone on %1 shares a game with Hub users or invites you.").arg(root.player.hubAddress || qsTr("your Hub"))
                font.pixelSize: 12
                color: Theme.gameMeta
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: root.list.length > 0
        implicitHeight: pickView.height + 2
        radius: 8
        color: Theme.gameGroupBg
        border.width: 1
        border.color: Theme.gameGroupBorder
        clip: true
        ListView {
            id: pickView
            objectName: "multiviewSessionView"
            x: 1
            y: 1
            width: parent.width - 2
            height: Math.min(contentHeight, root.maxListHeight)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.list
            ScrollBar.vertical: ScrollBar { policy: pickView.contentHeight > pickView.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            delegate: Item {
                id: prow
                required property var modelData
                required property int index
                readonly property bool isShown: root.shown.indexOf(modelData.sessionId) >= 0
                readonly property int tileNo: root.ctl.surfaceOrder.indexOf(modelData.sessionId) + 1
                objectName: "multiviewSession_" + modelData.sessionId
                width: ListView.view.width - (pickView.contentHeight > pickView.height ? 10 : 0)
                implicitHeight: 48
                height: 48
                Rectangle {
                    visible: prow.index < root.list.length - 1
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.gameGroupBorder
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        radius: 13
                        color: Theme.gameSegmentOn
                        FbLabel {
                            anchors.centerIn: parent
                            text: ((prow.modelData.who || prow.modelData.title || "?").charAt(0)).toUpperCase()
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.gameText
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 0
                        spacing: 0
                        FbLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: prow.modelData.title; font.pixelSize: 13; color: Theme.gameText }
                        FbLabel {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: prow.isShown ? prow.modelData.meta + qsTr(" · in tile %1").arg(prow.tileNo) : prow.modelData.meta
                            font.pixelSize: 11
                            color: prow.modelData.invited && !prow.isShown ? Theme.gameAccent : Theme.gameMeta
                        }
                    }
                    FbLabel {
                        objectName: "multiviewAdded_" + prow.modelData.sessionId
                        visible: prow.isShown
                        text: qsTr("✓ Added")
                        font.pixelSize: 12
                        color: Theme.gameOk
                    }
                    FbButton {
                        objectName: "multiviewAddButton_" + prow.modelData.sessionId
                        visible: !prow.isShown
                        implicitHeight: 28
                        implicitWidth: 56
                        Layout.minimumWidth: 56
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        text: qsTr("Add")
                        enabled: root.ctl.hubLink === "online" && root.ctl.canAddSurface && !prow.isShown
                        contentItem: Text {
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: parent.text
                            font: parent.font
                            color: parent.enabled ? Theme.textOnAccent : Theme.gameFaint
                        }
                        background: Rectangle { radius: 5; color: parent.enabled ? Theme.gameAccent : Theme.gameTrack }
                        onClicked: root.ctl.watch(prow.modelData.sessionId)
                    }
                }
            }
        }
    }

    Rectangle {
        objectName: "multiviewFullNote"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: root.full
        implicitHeight: fullText.implicitHeight + 20
        radius: 8
        color: Theme.neutralPillBg
        FbLabel {
            id: fullText
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: Theme.gameBadgeText
            text: root.firstFree() !== "" ? qsTr("Multiview is full. Remove a tile to add %1.").arg(root.firstFree())
                                          : qsTr("Multiview is full. Remove a tile to add another session.")
        }
    }
    FbLabel {
        objectName: "multiviewListNote"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        wrapMode: Text.WordWrap
        text: qsTr("Private sessions are not listed. Sound plays from one tile only.")
        font.pixelSize: 11
        color: Theme.gameFaint
    }
}
