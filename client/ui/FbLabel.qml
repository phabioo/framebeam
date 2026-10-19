import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Label {
    // Never demands its full text width from a layout (long text wraps or elides), so one label cannot push a block past its parent.
    Layout.minimumWidth: 0
    textFormat: Text.PlainText  // names and titles from peers, plugins and the Hub are never markup
    color: Theme.text
    font.pixelSize: Theme.fontBody
    // Qt Basic Label has no background color of its own; text wraps when needed.
}
