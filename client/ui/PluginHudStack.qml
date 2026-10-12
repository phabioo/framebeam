import QtQuick
import FrameBeam.Player

// The widgets of one HUD corner: grows away from the corner (bottom corners stack upwards), gap 6, at most 4.
Column {
    id: root
    property var widgets: []                       // entries of this corner, in order (first = nearest to the corner)
    property bool upwards: false                   // bottom corners
    property var plugins: null
    signal menuRequested(var entry, Item anchor)
    spacing: 6
    Repeater {
        model: Math.min(4, root.widgets.length)
        delegate: PluginHudWidget {
            id: w
            required property int index
            readonly property int rank: root.upwards ? Math.min(4, root.widgets.length) - 1 - index : index
            entry: root.widgets[rank]
            marker: root.plugins && entry ? root.plugins.originMarker(entry.origin) : ""
            onMenuRequested: (anchor) => root.menuRequested(w.entry, anchor)
        }
    }
}
