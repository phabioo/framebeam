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
    property var runRows: []
    color: Theme.bg

    function refreshRuns() { root.runRows = root.pluginId !== "" ? root.player.runs.runsOfPlugin(root.pluginId) : [] }
    Connections {
        target: root.player.runs
        function onRunsChanged() { root.refreshRuns() }
    }
    function refresh() {
        refreshRuns(); root.tree = root.pluginId !== "" ? root.plugins.slotTree(root.pluginId, "screen") : ({}) }
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
                Layout.maximumWidth: Theme.contentMaxWidth
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
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
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
                            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                            objectName: "pluginScreenName"
                            Layout.minimumWidth: 0
                            text: root.entry.name || ""
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontMeta
                            color: Theme.textMuted
                        }
                        FbMono {
                            textFormat: Text.PlainText
                            objectName: "pluginScreenVersion"
                            visible: (root.info.version || "") !== ""
                            text: root.info.version || ""
                            color: Theme.textMuted
                        }
                        PluginOriginPill {
                            objectName: "pluginScreenOrigin"
                            origin: root.entry.origin || "local"
                            text: root.info.originText || qsTr("Local file")
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                FbButton {
                    textFormat: Text.PlainText
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
                Layout.maximumWidth: Theme.contentMaxWidth   // rows are not stretched across a wide window (P29)
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                clip: true
                contentWidth: width
                contentHeight: (runsCol.visible ? runsCol.implicitHeight + 12 : 0) + block.implicitHeight + 8
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ColumnLayout {
                    id: runsCol
                    objectName: "pluginScreenRuns"
                    width: flick.width
                    spacing: 8
                    visible: root.player.runs.available
                    Eyebrow {
                        objectName: "pluginScreenRunsTitle"
                        visible: root.runRows.length > 0
                        text: qsTr("Your runs · %1").arg(root.runRows.length)
                    }
                    // Row count as the model: rows update in place (a rebuilt Repeater in a Qt 6.4 layout can crash).
                    Repeater {
                        model: root.runRows.length
                        delegate: Rectangle {
                            id: runRow
                            required property int index
                            readonly property var modelData: root.runRows[index] || ({})
                            objectName: "pluginScreenRun_" + (modelData.runId || "")
                            Layout.fillWidth: true
                            implicitHeight: 58
                            radius: Theme.radius10
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.borderCard
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    RowLayout {
                                        spacing: 8
                                        FbLabel { textFormat: Text.PlainText; text: runRow.modelData.name; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.maximumWidth: 360 }
                                        FbPill { textFormat: Text.PlainText; small: true; tone: runRow.modelData.tone; text: runRow.modelData.statusText }
                                    }
                                    FbLabel { textFormat: Text.PlainText; Layout.fillWidth: true; text: runRow.modelData.subText; color: Theme.textMuted; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }
                                }
                                FbButton {
                                    textFormat: Text.PlainText
                                    objectName: "pluginScreenOpenRun"
                                    implicitHeight: 32
                                    text: qsTr("Open run")
                                    onClicked: { root.player.showRuns(); root.player.runs.openRun(runRow.modelData.runId) }
                                }
                            }
                        }
                    }
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        objectName: "pluginScreenRunsHint"
                        visible: root.runRows.length > 0
                        Layout.fillWidth: true
                        text: qsTr("The run board, map and team open in game when you play from this run.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        objectName: "pluginScreenRunsEmpty"
                        visible: root.runRows.length === 0
                        Layout.fillWidth: true
                        FbLabel {
                            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                            Layout.fillWidth: true
                            text: qsTr("No %1 runs yet. Start a run with %2 in Runs.").arg(root.entry.title || "").arg(root.entry.name || "")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        FbButton { textFormat: Text.PlainText; objectName: "pluginScreenGoToRuns"; kind: "link"; text: qsTr("Go to Runs"); onClicked: root.player.showRuns() }
                    }
                }
                PluginBlock {
                    id: block
                    objectName: "pluginScreenBlock"
                    y: runsCol.visible ? runsCol.implicitHeight + 12 : 0
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
