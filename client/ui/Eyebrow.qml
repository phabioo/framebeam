import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Column header / section title: mono 11/500, uppercase, letter-spacing .08em.
Label {
    // Never demands its full text width from a layout (long text wraps or elides), so one label cannot push a block past its parent.
    Layout.minimumWidth: 0
    color: Theme.textFaint
    elide: Text.ElideRight
    font.letterSpacing: 0.9
    font.family: Theme.mono
    font.pixelSize: Theme.fontMono
    font.weight: Font.Medium
    font.capitalization: Font.AllUppercase
}
