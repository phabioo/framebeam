import QtQuick
import QtQuick.Layouts
import Uzume.App

// The error line under a form (square + sentence) with the sign-in wait of 8b-5c: a rate_limited error with `retryAfter`
// counts down as m:ss ("Too many wrong tries for Lena on this PC. Try again in 4:52."). Other errors show the controller's
// message. Nothing is shown without an error.
Item {
    id: root
    property string code: ""
    property string message: ""
    property int retryAfter: 0
    property string who: ""            // the person who is locked out, when known
    property int total: 0
    property int secsLeft: 0
    readonly property bool limited: code === "rate_limited" && retryAfter > 0
    readonly property string clock: Math.floor(secsLeft / 60) + ":" + String(secsLeft % 60).padStart(2, "0")
    readonly property string line: code === "" ? ""
        : !limited ? message
        : secsLeft > 0 ? (who !== "" ? qsTr("Too many wrong tries for %1 on this PC. Try again in %2.").arg(who).arg(clock)
                                 : qsTr("Too many wrong tries on this PC. Try again in %1.").arg(clock))
                   : qsTr("You can try again now.")
    visible: line !== ""
    implicitHeight: visible ? status.implicitHeight : 0
    implicitWidth: 320
    Layout.fillWidth: true

    function sync() {
        if (limited && retryAfter !== total) {
            total = retryAfter
            secsLeft = retryAfter
        } else if (!limited) {
            total = 0
            secsLeft = 0
        }
    }
    onRetryAfterChanged: sync()
    onCodeChanged: sync()
    Component.onCompleted: sync()
    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.limited && root.secsLeft > 0
        onTriggered: root.secsLeft = Math.max(0, root.secsLeft - 1)
    }
    UzStatusIcon {
        id: status
        objectName: "startNotice"
        width: parent.width
        kind: "error"
        text: root.line
        maxWidth: root.width
    }
}
