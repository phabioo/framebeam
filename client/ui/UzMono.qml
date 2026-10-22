import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Mono text (IBM Plex Mono): addresses, versions, paths, values (role `address` / `meta-11`).
Label {
    // Never demands its full text width from a layout (long text wraps or elides), so one label cannot push a block past its parent.
    Layout.minimumWidth: 0
    color: Theme.muted
    font.family: Theme.mono
    font.pixelSize: Theme.fontMeta
}
