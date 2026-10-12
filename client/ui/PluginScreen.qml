import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Plugin sidebar screen (docs/design/plugins.md 4f): title row (title, icon, plugin name with its origin badge, "Plugin
// settings"), then the plugin's "screen" tree full width, scrolling vertically. It follows the app theme.
Rectangle {
    id: root
    required property PlayerController player
    readonly property PluginsController plugins: player.plugins
    readonly property string pluginId: player.pluginPageId
    readonly property var entry: {
        var e = plugins.sidebarEntries
        for (var i = 0; i < e.length; ++i) if (e[i].pluginId === root.pluginId) return e[i]
        return ({})
    }
    readonly property var info: {
        var l = plugins.plugins
        for (var i = 0; i < l.length; ++i) if (l[i].id === root.pluginId) return l[i]
        return ({})
    }
    property var tree: ({})
    color: Theme.bg

    function refresh() { root.tree = root.pluginId !== "" ? root.plugins.slotTree(root.pluginId, "screen") : ({}) }
    onPluginIdChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: root.plugins
        function onSlotChanged(id, slot) { if (id === root.pluginId && slot === "screen") root.refresh() }
        function onSidebarEntriesChanged() { root.refresh() }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            player: root.player
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.margins: 28
            Layout.leftMargin: 36
            Layout.rightMargin: 36
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 12
                PluginTabIcon {
                    Layout.alignment: Qt.AlignVCenter
                    dark: Theme.dark
                    size: 32
                    chipRadius: 8
                    name: root.entry.name || root.entry.title || ""
                    icon: root.entry.icon || ""
                }
                ColumnLayout {
                    Layout.minimumWidth: 0
                    spacing: 4
                    FbLabel {
                        objectName: "pluginScreenTitle"
                        text: root.entry.title || ""
                        font.pixelSize: Theme.fontPage
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.4
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 8
                        FbLabel {
                            objectName: "pluginScreenName"
                            Layout.minimumWidth: 0
                            text: root.entry.name || ""
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontMeta
                            color: Theme.textMuted
                        }
                        FbPill {
                            objectName: "pluginScreenOrigin"
                            small: true
                            tone: root.entry.origin === "official" ? "ok" : (root.entry.origin === "unverified" ? "warn" : "neutral")
                            text: root.info.originText || qsTr("Local file")
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                FbButton {
                    objectName: "pluginScreenSettings"
                    implicitHeight: 36
                    text: qsTr("Plugin settings")
                    onClicked: root.player.showPluginSettings(root.pluginId)
                }
            }

            Flickable {
                id: flick
                objectName: "pluginScreenFlick"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                clip: true
                contentWidth: width
                contentHeight: block.implicitHeight + 8
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                PluginBlock {
                    id: block
                    objectName: "pluginScreenBlock"
                    width: flick.width
                    height: implicitHeight
                    node: root.tree
                    dark: Theme.dark
                    pluginId: root.pluginId
                    onAction: (a, v) => root.plugins.sendAction(root.pluginId, "screen", a, v)
                }
            }
        }
    }
}
