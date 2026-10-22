import QtQuick
import QtQuick.Layouts
import Uzume.App

// Plugin content in the run view (Runs screen, design 4j-app-5): the tab of one plugin of the run (`pluginId`) with its screen
// tree (ui.screen) or run tree (ui.run), under a line "from <plugin>" with the origin marker for non-official plugins. Follows the
// app theme. `statusOnly` shows just the line that says while the plugins are fetched or when one failed (with a retry).
ColumnLayout {
    id: root
    objectName: "pluginRunSection"
    required property PlayerController player
    property string pluginId: ""
    property bool statusOnly: false
    readonly property var sections: root.statusOnly ? [] : player.plugins.runSections.filter(sec => root.pluginId === "" || sec.pluginId === root.pluginId)
    readonly property string problems: player.plugins.runViewProblems
    readonly property string viewState: player.runs.runViewState
    visible: sections.length > 0 || viewState !== "" || problems !== ""
    spacing: 18

    FbLabel {
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        objectName: "pluginRunStatus"
        Layout.fillWidth: true
        visible: root.viewState !== "" || root.problems !== ""
        wrapMode: Text.WordWrap
        color: root.viewState === "error" || (root.viewState === "" && root.problems !== "") ? Theme.errorText : Theme.textMuted
        font.pixelSize: Theme.fontSmall
        text: root.viewState !== "" ? root.player.runs.runViewText : root.problems
    }
    FbButton {
        objectName: "pluginRunRetry"
        visible: root.viewState === "error"
        text: qsTr("Try again")
        onClicked: root.player.runs.retryRunView()
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
                FbLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "pluginRunFrom"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("from %1").arg(sec.modelData.name)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideRight
                }
                PluginOriginPill {
                    objectName: "pluginRunOrigin"
                    origin: sec.modelData.origin || "local"
                    text: sec.modelData.originText || sec.marker
                }
            }
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
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
                onAction: (action, value) => root.player.plugins.sendAction(sec.modelData.pluginId, sec.modelData.slot || "run", action, value)
            }
        }
    }
}
