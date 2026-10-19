import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// One notice (docs/design/plugins.md 4g-2): grid 28 | flex | auto, padding 12, radius 10; tone info / important (cyan) or
// warning (amber); source line 11/500, message 13/1.45, actions 28 high, "⋯" (mute menu) and "×" 22 x 22.
Item {
    id: root
    required property var toast                    // PluginsController.toasts entry
    property string marker: ""                     // "Unverified" / "Local" for non-official plugins
    signal dismissRequested()
    signal actionPicked(var act)
    signal menuRequested(Item anchor)
    readonly property bool warn: toast.tone === "warning"
    readonly property bool host: !!toast.host
    readonly property bool hovered: hover.hovered

    implicitWidth: 390
    implicitHeight: card.height
    height: card.height
    objectName: "toast_" + toast.toastId

    Repeater {  // soft shadow (0 18px 40px rgba(0,0,0,.5)): stacked low-alpha layers
        model: 3
        Rectangle {
            required property int index
            x: -index * 3
            y: 10 + index * 2
            width: card.width + index * 6
            height: card.height + index * 3
            radius: 10 + index * 3
            color: "#14000000"
            z: -1
        }
    }
    Rectangle {
        id: card
        width: parent.width
        height: grid.implicitHeight + 24
        radius: 10
        color: root.warn ? Theme.gamePluginWarnBg : Theme.gameOnBg
        border.width: 1
        border.color: root.warn ? Theme.gamePluginWarnLine : Theme.gameOnBorder
        HoverHandler { id: hover }

        RowLayout {
            id: grid
            x: 12; y: 12
            width: parent.width - 24
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                radius: 7
                color: root.warn ? "#3a3118" : "#1b3a42"
                Text {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    anchors.centerIn: parent
                    text: root.host ? "◆" : (root.toast.tone === "info" ? "i" : "!")
                    font.pixelSize: root.host ? 12 : 15
                    font.weight: Font.Bold
                    color: root.warn ? Theme.gamePluginWarn : Theme.gameAccent
                }
            }
            ColumnLayout {
                id: toastCol
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        objectName: "toastSource"
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: root.host ? qsTr("%1 · %2").arg(root.toast.pluginName).arg(root.toast.title)
                                        : root.toast.tone === "info" ? root.toast.pluginName
                                        : qsTr("%1 · %2").arg(root.toast.pluginName).arg(root.warn ? qsTr("warning") : qsTr("important"))
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: root.warn ? Theme.gamePluginWarn : Theme.gameAccent
                    }
                    Rectangle {
                        objectName: "toastMarker"
                        visible: root.marker !== ""
                        Layout.preferredWidth: markerLabel.implicitWidth + 10
                        Layout.preferredHeight: 15
                        radius: 4
                        color: "transparent"
                        border.width: 1
                        border.color: root.warn ? Theme.gamePluginWarnLine : Theme.gameKeyLine
                        FbLabel { textFormat: Text.PlainText; id: markerLabel; anchors.centerIn: parent; text: root.marker; font.pixelSize: 10; color: root.warn ? Theme.gamePluginWarn : Theme.gameTextMuted }
                    }
                    Item { Layout.fillWidth: true }
                }
                FbLabel {
                    objectName: "toastText"
                    Layout.fillWidth: true
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                    lineHeight: 1.45
                    textFormat: Text.PlainText
                    text: (!root.host && root.toast.title ? root.toast.title + "\n" : "") + (root.toast.text || "")
                    font.pixelSize: 13
                    color: Theme.gameText
                }
                // The Flow sits in a plain Item whose width is the column's: a Flow directly in the layout kept the width of an
                // earlier layout pass and ran past the notice with wide fonts.
                Item {
                    Layout.preferredWidth: toastCol.width
                    Layout.maximumWidth: toastCol.width
                    Layout.minimumWidth: 0
                    Layout.topMargin: 2
                    visible: (root.toast.actions || []).length > 0
                    implicitHeight: actionFlow.implicitHeight
                    Flow {
                        id: actionFlow
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: root.toast.actions || []
                            delegate: Rectangle {
                                id: act
                                required property var modelData
                                required property int index
                                objectName: "toastAction_" + modelData.action
                                width: Math.min(actLabel.implicitWidth + 24, act.parent ? act.parent.width : 100000)   // never wider than the notice (wide fonts)
                                height: 28
                                radius: 6
                                color: act.index === 0 ? (root.warn ? Theme.gamePluginWarn : Theme.gameAccent) : (actHover.hovered ? Theme.gameSegmentOn : "transparent")
                                border.width: act.index === 0 ? 0 : 1
                                border.color: Theme.gameKeyLine
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.label
                                FbLabel {
                                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                    id: actLabel
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, act.width - 24)
                                    elide: Text.ElideRight
                                    text: act.modelData.label
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: act.index === 0 ? "#161512" : Theme.gameText
                                }
                                HoverHandler { id: actHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: root.actionPicked(act.modelData) }
                            }
                        }
                    }
                }
            }
            Row {
                Layout.alignment: Qt.AlignTop
                spacing: 2
                Rectangle {
                    id: moreBtn
                    objectName: "toastMore"
                    visible: !root.host
                    width: 22; height: 22; radius: 6
                    color: moreHover.hovered ? Theme.gameSegmentOn : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Notice menu")
                    Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: "⋯"; font.pixelSize: 15; color: Theme.gameTextMuted }
                    HoverHandler { id: moreHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.menuRequested(moreBtn) }
                }
                Rectangle {
                    objectName: "toastDismiss"
                    width: 22; height: 22; radius: 6
                    color: closeHover.hovered ? Theme.gameSegmentOn : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Dismiss")
                    Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: "×"; font.pixelSize: 16; color: Theme.gameTextMuted }
                    HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.dismissRequested() }
                }
            }
        }
    }
}
