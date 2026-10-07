import QtQuick.Controls.Basic
import QtQuick.Layouts

Label {
    // Never demands its full text width from a layout (long text wraps or elides), so one label cannot push a block past its parent.
    Layout.minimumWidth: 0
    color: Theme.textMuted
    font.family: Theme.mono
    font.pixelSize: Theme.fontMeta
}
