import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Plugin detail (round 3, 4a-4, P21/P22/D16): a 460 px second-column sheet over the plugin list. One primary control (the
// on/off switch), the origin note, permissions, relations (Needs / Adds) and "Uninstall…" inside the "⋯" menu. The
// plugin settings schema is not part of Plugin API v1 yet, so the sheet draws no SETTINGS section (open point).
Popup {
    id: root
    required property PlayerController player
    property string pluginId: ""
    property string hubName: ""
    property Item overlayParent: null
    readonly property var ctl: player.plugins
    readonly property var info: {
        const l = root.ctl.plugins
        for (let i = 0; i < l.length; ++i) if (l[i].id === root.pluginId) return l[i]
        return null
    }
    property bool confirmUninstall: false
    signal enableRequested(string id)

    parent: root.overlayParent
    x: parent ? parent.width - width : 0
    y: 0
    width: Math.min(460, parent ? parent.width : 460)
    height: parent ? parent.height : 600
    modal: false
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onClosed: root.confirmUninstall = false
    onInfoChanged: if (root.opened && root.info === null) root.close()

    function monogram(name: string): string {
        const w = name.split(/[^A-Za-z0-9]+/).filter(s => s.length > 0)
        return ((w[0] ? w[0][0] : "?") + (w[1] ? w[1][0] : "")).toUpperCase()
    }

    background: Rectangle {
        color: Theme.popupBg
        border.width: 1
        border.color: Theme.borderPopup
    }

    contentItem: Flickable {
        id: flick
        objectName: "pluginDetailSheet"
        clip: true
        contentWidth: width
        contentHeight: col.implicitHeight + 40
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col
            x: 24
            y: 20
            width: flick.width - 48
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                UzButton {
                    textFormat: Text.PlainText
                    objectName: "pluginDetailBack"
                    kind: "link"
                    text: "‹ " + qsTr("Plugins")
                    onClicked: root.close()
                }
                Item { Layout.fillWidth: true }
                // The menu sits in the header next to the close button, not alone in the empty lower part of the sheet (R-P17).
                MoreMenu {
                    objectName: "pluginDetailMenu"
                    tooltip: qsTr("More actions")
                    items: [{ name: "uninstall", label: root.info && root.info.devLinked ? qsTr("Unlink folder…") : qsTr("Uninstall…"), danger: true, divider: false }]
                    onActivated: name => { if (name === "uninstall") root.confirmUninstall = true }
                }
                UzButton {
                    textFormat: Text.PlainText
                    objectName: "pluginDetailClose"
                    kind: "link"
                    text: "✕"
                    onClicked: root.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Rectangle {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: Theme.radius10
                    color: Theme.neutralPillBg
                    Text {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        anchors.centerIn: parent
                        text: root.info ? root.monogram(root.info.name) : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    UzLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        objectName: "pluginDetailName"
                        Layout.fillWidth: true
                        text: root.info ? root.info.name : ""
                        font.pixelSize: Theme.fontSection
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    UzLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        objectName: "pluginDetailVersion"
                        Layout.fillWidth: true
                        text: {
                            if (!root.info) return ""
                            const parts = [root.info.version]
                            parts.push(root.info.updateVersion !== "" ? qsTr("update to %1 available").arg(root.info.updateVersion) : qsTr("up to date"))
                            if (root.info.hubCopy && root.hubName !== "") parts.push(qsTr("from %1").arg(root.hubName))
                            return parts.join(" · ")
                        }
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                }
            }

            // On / off: the one primary control.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: onRow.implicitHeight + 28
                radius: Theme.radius10
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderCard
                RowLayout {
                    id: onRow
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        UzLabel {
                            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                            objectName: "pluginDetailState"
                            text: root.info && root.info.enabled ? qsTr("On") : qsTr("Off")
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                        }
                        UzLabel {
                            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                            objectName: "pluginDetailStateNote"
                            Layout.fillWidth: true
                            text: {
                                if (!root.info) return ""
                                if (root.info.problemsText !== "") return root.info.problemsText
                                if (root.info.depsOff.length > 0 && !root.info.enabled) return qsTr("%1 turns on too (needed)").arg(root.info.depsOff.join(", "))
                                return ""
                            }
                            visible: text !== ""
                            wrapMode: Text.WordWrap
                            color: root.info && root.info.problemsText !== "" ? Theme.pluginWarn : Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                    UzToggle {
                        objectName: "pluginDetailToggle"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: 40
                        enabled: root.info ? root.info.canToggle : false
                        opacity: enabled ? 1 : 0.4
                        checked: root.info ? root.info.enabled : false
                        onToggled: {
                            if (!root.info) return
                            if (checked && (root.info.needsConfirm || root.info.depsOff.length > 0)) {
                                const id = root.pluginId
                                checked = Qt.binding(() => root.info ? root.info.enabled : false)
                                root.enableRequested(id)
                            } else {
                                root.ctl.setPluginEnabled(root.pluginId, checked)
                            }
                        }
                    }
                }
            }

            // Origin note.
            Rectangle {
                visible: root.info && root.info.origin !== "official"
                Layout.fillWidth: true
                implicitHeight: originRow.implicitHeight + 24
                radius: Theme.radius10
                color: root.info && root.info.origin === "unverified" ? Theme.pluginWarnBg : Theme.surface
                border.width: 1
                border.color: root.info && root.info.origin === "unverified" ? Theme.pluginWarnLine : Theme.borderCard
                RowLayout {
                    id: originRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    PluginOriginPill {
                        objectName: "pluginDetailOrigin"
                        origin: root.info ? root.info.origin : "local"
                        text: root.info ? root.info.originText : ""
                    }
                    UzLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        Layout.fillWidth: true
                        text: root.info && root.info.origin === "unverified"
                              ? qsTr("Not checked by Uzume. Turn it on only if you trust the admin of your Hub.")
                              : qsTr("Loaded from a file on this device. It is not checked by Uzume.")
                        wrapMode: Text.WordWrap
                        color: Theme.pluginWarnText
                        font.pixelSize: Theme.fontMeta
                    }
                }
            }

            Eyebrow { text: qsTr("Permissions"); Layout.topMargin: 4 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: root.info ? root.info.permissionList : []
                    delegate: UzLabel {
                        textFormat: Text.PlainText
                        required property string modelData
                        objectName: "pluginDetailPermission"
                        Layout.fillWidth: true
                        text: "• " + modelData
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: !root.info || root.info.permissionList.length === 0
                    text: qsTr("No special permissions")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }

            Eyebrow {
                visible: root.info && (root.info.needsLines.length > 0 || root.info.adds !== "")
                text: qsTr("Relations")
                Layout.topMargin: 4
            }
            Repeater {
                model: root.info ? root.info.needsLines : []
                delegate: UzLabel {
                    textFormat: Text.PlainText
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
            UzLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "pluginAdds"
                visible: root.info && root.info.adds !== ""
                Layout.fillWidth: true
                text: root.info ? root.info.adds : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            // Uninstall confirmation (the "⋯" menu opens it).
            Rectangle {
                objectName: "pluginUninstallPanel"
                visible: root.confirmUninstall
                Layout.fillWidth: true
                implicitHeight: unCol.implicitHeight + 24
                radius: Theme.radius10
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderCard
                ColumnLayout {
                    id: unCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    UzLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        Layout.fillWidth: true
                        text: root.info && root.info.devLinked ? qsTr("Remove the link? The folder itself is kept.")
                                                               : qsTr("Remove this plugin from this device? Run data stays on the Hub.")
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        UzButton {
                            textFormat: Text.PlainText
                            objectName: "pluginUninstallCancel"
                            implicitHeight: 32
                            text: qsTr("Cancel")
                            onClicked: root.confirmUninstall = false
                        }
                        UzButton {
                            textFormat: Text.PlainText
                            objectName: "pluginUninstallConfirm"
                            implicitHeight: 32
                            kind: "primary"
                            text: qsTr("Uninstall")
                            onClicked: {
                                const id = root.pluginId
                                root.confirmUninstall = false
                                root.close()
                                root.ctl.uninstall(id)
                            }
                        }
                    }
                }
            }

            RowLayout {
                visible: root.info !== null && root.info.updateVersion !== ""
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    Layout.fillWidth: true
                    text: root.info && root.info.updateVersion !== "" ? qsTr("Update to %1 is available").arg(root.info.updateVersion) : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                }
                UzButton {
                    textFormat: Text.PlainText
                    objectName: "pluginDetailUpdate"
                    visible: root.info && root.info.updateVersion !== ""
                    implicitHeight: 32
                    text: root.info ? qsTr("Update to %1").arg(root.info.updateVersion) : ""
                    onClicked: root.ctl.installHubPlugin(root.pluginId, root.info.updateVersion)
                }
            }
        }
    }
}
