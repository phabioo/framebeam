import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Section label (design-system.md role `section-label`): Plex Mono 500 10, uppercase, +14 % tracking, `dim` on ground.
Label {
    // Never demands its full text width from a layout (long text wraps or elides), so one label cannot push a block past its parent.
    Layout.minimumWidth: 0
    color: Theme.dim
    elide: Text.ElideRight
    font.letterSpacing: Theme.roles.sectionLabel.spacing
    font.family: Theme.mono
    font.pixelSize: Theme.roles.sectionLabel.size
    font.weight: Font.Medium
    font.capitalization: Font.AllUppercase
}
