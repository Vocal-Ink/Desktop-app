import QtQuick
import QtQuick.Controls.Basic

// Tooltip in the app's style: short, delayed, never covers the pointer.
ToolTip {
    id: tip
    delay: 450
    timeout: 6000
    padding: Theme.s2
    leftPadding: Theme.s3
    rightPadding: Theme.s3

    contentItem: Txt {
        text: tip.text
        role: "caption"
        color: Theme.text
        wrapMode: Text.NoWrap
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.raised
        border.color: Theme.line
        border.width: Theme.hairline
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }
}
