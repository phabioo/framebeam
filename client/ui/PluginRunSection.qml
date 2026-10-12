import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Plugin content in the run detail (Runs screen, design 4j-2): one block per plugin of the run that declares ui.run, under a
// header with its title (and the origin marker for non-official plugins). Follows the app theme. Hidden when no plugin of the
// run declares ui.run; a short line says while the plugins are fetched or when one failed.
ColumnLayout {
    id: root
    objectName: "pluginRunSection"
    required property PlayerController player
    readonly property var sections: player.plugins.runSections
    readonly property string problems: player.plugins.runViewProblems
    readonly property string viewState: player.runs.runViewState
    visible: sections.length > 0 || viewState !== "" || problems !== ""
    spacing: 18

    FbLabel {
        objectName: "pluginRunStatus"
        Layout.fillWidth: true
        visible: root.viewState !== "" || root.problems !== ""
        wrapMode: Text.WordWrap
        color: root.viewState === "error" || (root.viewState === "" && root.problems !== "") ? Theme.errorText : Theme.textMuted
        font.pixelSize: Theme.fontSmall
        text: root.viewState !== "" ? root.player.runs.runViewText : root.problems
    }

    Repeater {
        model: root.sections
        delegate: ColumnLayout {
            id: sec
            required property var modelData
            readonly property string marker: root.player.plugins.originMarker(modelData.origin || "")
            objectName: "pluginRun_" + modelData.pluginId
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 8
                Eyebrow {
                    objectName: "pluginRunTitle"
                    Layout.fillWidth: true
                    text: sec.modelData.title || sec.modelData.name
                }
                FbPill {
                    objectName: "pluginRunOrigin"
                    visible: sec.marker !== ""
                    tone: "warn"
                    text: sec.marker
                }
            }
            FbLabel {
                objectName: "pluginRunNote"
                Layout.fillWidth: true
                visible: sec.modelData.paused === true || sec.modelData.loading === true
                color: sec.modelData.paused === true ? Theme.errorText : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                text: sec.modelData.paused === true ? qsTr("%1 was paused after an error.").arg(sec.modelData.name) : qsTr("Loading…")
            }
            PluginBlock {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: Object.keys(sec.modelData.tree).length > 0
                node: sec.modelData.tree
                dark: Theme.dark
                pluginId: sec.modelData.pluginId
                onAction: (action, value) => root.player.plugins.sendAction(sec.modelData.pluginId, "run", action, value)
            }
        }
    }
}
