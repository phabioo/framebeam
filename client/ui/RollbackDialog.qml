import QtQuick
import QtQuick.Layouts
import Uzume.App

// "Go back to <version>?" (design 6c-2): what happens, what stays, what is turned off. Modal; "Cancel" has the focus, Esc
// cancels. While a game or Session runs the primary is muted and the dialog says why. The Player closes, installs the
// older version and starts again; settings and saves are not touched.
Item {
    id: root
    required property var updates
    property string version: ""
    property var info: ({})
    readonly property bool shown: version !== ""
    readonly property bool blocked: root.updates.busy || !root.info.canGoBack

    function openFor(v) {
        info = updates.rollbackInfo(v)
        version = v
    }
    function close() { version = "" }

    visible: shown
    anchors.fill: parent
    z: 100

    onShownChanged: if (shown) cancelButton.forceActiveFocus()

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    FocusScope {
        anchors.fill: parent
        focus: root.shown
        Keys.onEscapePressed: root.close()

        Rectangle {
            objectName: "rollbackDialog"
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 48)
            implicitHeight: col.implicitHeight + 56
            height: Math.min(implicitHeight, parent.height - 32)
            radius: Theme.radius12
            color: Theme.bgPanel
            border.width: 1
            border.color: Theme.borderInput

            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 28
                spacing: 14

                UzLabel {
                    objectName: "rollbackTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Go back to %1?").arg(root.version)
                    font.pixelSize: Theme.fontDialog
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                UzLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Uzume closes, installs %1 and starts again. About 30 s.").arg(root.version)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
                UzLabel {
                    objectName: "rollbackBusy"
                    visible: root.updates.busy
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: "⊘ " + (root.info.busyText || "")
                    color: Theme.pluginWarn
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
                UzLabel {
                    objectName: "rollbackUnsupported"
                    visible: !root.info.canGoBack
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.updates.goBackUnavailableText
                    color: Theme.pluginWarn
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSidebar }

                Repeater {
                    model: [
                        { name: "Settings", title: qsTr("Settings"),
                          text: qsTr("Kept. Newer settings are ignored and come back after the update.") },
                        { name: "Saves", title: qsTr("Saves and states"),
                          text: qsTr("Not touched. They are on your server.") },
                        { name: "Plugins", title: qsTr("Plugins"),
                          text: root.info.pluginLines && root.info.pluginLines.length > 0
                                ? root.info.pluginLines.join("\n") : qsTr("All keep working."),
                          warn: root.info.pluginLines && root.info.pluginLines.length > 0 },
                        { name: "Updates", title: qsTr("Updates"),
                          text: qsTr("Paused until you resume them. %1 stays available.").arg(root.updates.currentVersion) }
                    ]
                    delegate: RowLayout {
                        id: infoRow
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 16
                        UzLabel {
                            Layout.preferredWidth: 120
                            Layout.alignment: Qt.AlignTop
                            text: infoRow.modelData.title
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontBody
                        }
                        UzLabel {
                            objectName: "rollbackRow" + infoRow.modelData.name
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: infoRow.modelData.text
                            color: infoRow.modelData.warn === true ? Theme.pluginWarn : Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                RowLayout {
                    id: buttonRow
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 10
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    UzButton {
                        id: cancelButton
                        objectName: "rollbackCancel"
                        implicitHeight: 40
                        text: qsTr("Cancel")
                        KeyNavigation.tab: goButton
                        KeyNavigation.backtab: goButton
                        Keys.onReturnPressed: root.close()
                        onClicked: root.close()
                    }
                    UzButton {
                        id: goButton
                        objectName: "rollbackConfirm"
                        implicitHeight: 40
                        kind: "primary"
                        enabled: !root.blocked
                        opacity: root.blocked ? 0.5 : 1   // muted while a game runs
                        Layout.fillWidth: false
                        Layout.minimumWidth: 0
                        // Shrinks (and elides) a long version label by itself; no binding on the row's own width, that
                        // made the layout rearrange recursively.
                        Layout.preferredWidth: Math.min(implicitWidth, 360)
                        Layout.maximumWidth: 360
                        text: qsTr("Go back to %1").arg(root.version)
                        KeyNavigation.tab: cancelButton
                        KeyNavigation.backtab: cancelButton
                        Keys.onReturnPressed: if (!root.blocked) clicked()
                        onClicked: {
                            const v = root.version
                            root.close()
                            root.updates.goBack(v)
                        }
                    }
                }
            }
        }
    }
}
