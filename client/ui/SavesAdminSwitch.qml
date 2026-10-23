import QtQuick
import QtQuick.Layouts
import Uzume.App

// Saves, admin part (accounts-and-server.md 3s-app-1): the "Show saves of: Mine / All users" switch at the top of the Saves view.
// "Mine" is the view below it; "All users" opens SavesAdminSheet over the window (the table of every person's saves) and
// returns to "Mine" when the sheet closes. The caller shows the switch for admins outside the game only.
// objectNames: savesAdminSwitch, savesScopeSegment, savesScopeMine, savesScopeAll.
Item {
    id: root
    objectName: "savesAdminSwitch"
    required property PlayerController player
    implicitHeight: row.implicitHeight
    // Label above the segments: the Saves pane is narrow (about 290 px), and with wide (Windows) fonts label and segments
    // side by side overflow it. fillWidth + maximumWidth lets the segments shrink (elide) instead of growing past the pane.
    ColumnLayout {
        id: row
        width: parent.width
        spacing: Theme.space1
        UzMono {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            elide: Text.ElideRight
            text: qsTr("Show saves of").toUpperCase()
        }
        UzSegmented {
            objectName: "savesScopeSegment"
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            options: [{ value: "mine", label: qsTr("Mine"), name: "savesScopeMine" }, { value: "all", label: qsTr("All users"), name: "savesScopeAll" }]
            current: sheet.shown ? "all" : "mine"
            onPicked: (value) => { if (value === "all") sheet.open(); else sheet.close() }
        }
    }
    SavesAdminSheet { id: sheet; player: root.player }
}
