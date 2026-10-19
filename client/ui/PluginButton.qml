import QtQuick
import QtQuick.Layouts

// button block: variant primary | secondary | quiet | danger; emits action(action, value) after the optional confirmation.
PluginFocusable {
    id: root
    property var node: ({})
    property string pluginId: ""
    readonly property string variant: node.variant || "secondary"
    signal action(string action, var value)

    ringRadius: Theme.radius7
    implicitHeight: 34
    implicitWidth: Math.max(72, label.implicitWidth + 28)
    Layout.minimumWidth: implicitWidth

    function fire() {
        if (node.confirm) {
            dialogLoader.active = true
            dialogLoader.item.open()
        } else {
            root.action(node.action || "", node.value)
        }
    }
    onActivated: fire()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius7
        readonly property bool filled: root.variant === "primary" || root.variant === "danger"
        color: root.variant === "primary" ? (root.pressed ? Qt.darker(Theme.blockColor("accent", root.dark), 1.18) : (root.hovered ? Qt.lighter(Theme.blockColor("accent", root.dark), 1.1) : Theme.blockColor("accent", root.dark)))
               : root.variant === "danger" ? (root.hovered || root.pressed ? Theme.toneLine("danger", root.dark) : Theme.toneBgFor("danger", root.dark))
               : (root.pressed ? Theme.blockColor("border", root.dark) : (root.hovered ? Theme.blockColor("hover", root.dark) : "transparent"))
        border.width: root.variant === "quiet" || root.variant === "primary" ? 0 : 1
        border.color: root.variant === "danger" ? Theme.toneLine("danger", root.dark) : Theme.blockColor("borderButton", root.dark)
        Text {
            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
            id: label
            objectName: "pluginButtonLabel"
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 16)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            text: root.node.label || ""
            font.pixelSize: Theme.fontBody
            font.weight: root.variant === "primary" ? Font.DemiBold : Font.Medium
            color: root.variant === "primary" ? Theme.blockColor("textOnAccent", root.dark)
                   : root.variant === "danger" ? Theme.toneFg("danger", root.dark)
                   : root.variant === "quiet" ? Theme.blockColor("textMuted", root.dark)
                   : Theme.blockColor("text", root.dark)
        }
    }

    Loader {
        id: dialogLoader
        active: false
        sourceComponent: PluginConfirmDialog {
            dark: root.dark
            confirm: root.node.confirm || ({})
            onAccepted: root.action(root.node.action || "", root.node.value)
        }
    }
}
