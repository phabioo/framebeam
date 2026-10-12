import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Plugin sections in the game detail column (design 4h-2): one block per plugin that set its "detail" slot, under a header
// with the plugin name (and the origin marker for non-official plugins). Nothing is shown while no plugin has content.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property var sections: player.plugins.detailSections
    visible: sections.length > 0
    spacing: 18

    Repeater {
        model: root.sections
        delegate: ColumnLayout {
            id: sec
            required property var modelData
            readonly property string marker: root.player.plugins.originMarker(modelData.origin || "")
            objectName: "pluginDetail_" + modelData.pluginId
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 8
                Eyebrow {
                    objectName: "pluginDetailName"
                    Layout.fillWidth: true
                    text: sec.modelData.name
                }
                FbPill {
                    objectName: "pluginDetailOrigin"
                    visible: sec.marker !== ""
                    tone: "warn"
                    text: sec.marker
                }
            }
            PluginBlock {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                node: sec.modelData.tree
                dark: Theme.dark
                pluginId: sec.modelData.pluginId
                onAction: (action, value) => root.player.plugins.sendAction(sec.modelData.pluginId, "detail", action, value)
            }
        }
    }
}
