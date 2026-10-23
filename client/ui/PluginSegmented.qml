import QtQuick

// segmented block: 2 to 3 short options { value, label }; selected = value. Emits action(action, value).
// Drawn by UzSegmented (design-system.md "Segmented").
Item {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    implicitHeight: seg.implicitHeight
    implicitWidth: seg.implicitWidth

    // UzSegmented compares string values; the plugin's own value (number or string) is sent back unchanged.
    function originalValue(v) {
        const o = root.node.options || []
        for (let i = 0; i < o.length; ++i) {
            if (String(o[i].value) === String(v)) return o[i].value
        }
        return v
    }

    UzSegmented {
        id: seg
        anchors.fill: parent
        options: (root.node.options || []).map(o => ({ value: String(o.value), label: String(o.label), name: "pluginSegment_" + o.value }))
        current: root.node.selected === undefined ? "" : String(root.node.selected)
        onPicked: (value) => root.action(root.node.action || "", root.originalValue(value))
    }
}
