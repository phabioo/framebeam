import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// In-game screen layout switch (3g-3i, D12): segment with mini icons. layouts = the values the system offers
// ("stacked" | "side" | "top"); applies to the running game only.
Rectangle {
    id: root
    property var layouts: []
    property string current: "stacked"
    property bool compact: false   // icons only (narrow headers)
    signal picked(string value)

    readonly property var labels: ({ "stacked": qsTr("Stacked"), "side": qsTr("Side by side"), "top": qsTr("Top only") })
    readonly property var names: ({ "stacked": "layoutStacked", "side": "layoutSide", "top": "layoutTop" })

    visible: layouts.length > 1
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 6
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderInput

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        Repeater {
            model: root.layouts
            delegate: Rectangle {
                id: opt
                required property string modelData
                objectName: root.names[modelData] || ""
                readonly property bool active: root.current === modelData
                Layout.fillHeight: true
                implicitWidth: content.implicitWidth + (root.compact ? 16 : 20)
                Accessible.role: Accessible.Button
                Accessible.name: root.labels[modelData] || modelData
                radius: 6
                color: active ? Theme.surfaceRaised : "transparent"
                border.width: active ? 1 : 0
                border.color: Theme.borderButton
                RowLayout {
                    id: content
                    anchors.centerIn: parent
                    spacing: 7
                    // Mini icon: two 9 x 7 boxes, in a column or a row; for "Top only" the second box is faint.
                    Grid {
                        columns: opt.modelData === "side" ? 2 : 1
                        spacing: 2
                        Repeater {
                            model: 2
                            delegate: Rectangle {
                                required property int index
                                width: 9
                                height: 7
                                color: "transparent"
                                border.width: 1.5
                                border.color: opt.active ? Theme.text : Theme.textMuted
                                opacity: opt.modelData === "top" && index === 1 ? 0.25 : 1
                            }
                        }
                    }
                    Text {
                        visible: !root.compact
                        text: root.labels[opt.modelData] || opt.modelData
                        font.pixelSize: 13
                        font.weight: opt.active ? Font.DemiBold : Font.Medium
                        color: opt.active ? Theme.text : Theme.textMuted
                    }
                }
                TapHandler { onTapped: root.picked(opt.modelData) }
            }
        }
    }
}
