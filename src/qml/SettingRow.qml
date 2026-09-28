import QtQuick
import QtQuick.Layouts

// One setting: what it is and what it does on the left, the control on the right.
// Drops the control under the text when the row gets narrow.
// Wrapped text makes the height depend on the width the parent layout hands
// out, and Qt Quick Layouts ignore height changes made during their own pass
// (the row kept the height its text had at width 0). So the new height is
// reported just after the pass.
Item {
    id: row
    property string title
    property string description: ""
    property bool stacked: width < Math.round(520 * Theme.scale)
    default property alias control: slot.data

    readonly property real gap: Theme.s5

    Layout.fillWidth: true
    implicitWidth: Math.round(320 * Theme.scale) + gap + slot.implicitWidth
    readonly property real naturalHeight: stacked ? info.implicitHeight + Theme.s2 + slot.implicitHeight
                                                  : Math.max(info.implicitHeight, slot.implicitHeight)
    implicitHeight: settledHeight
    property real settledHeight: naturalHeight
    onNaturalHeightChanged: Qt.callLater(() => { settledHeight = naturalHeight })

    Column {
        id: info
        width: row.stacked ? row.width : Math.max(0, row.width - slot.implicitWidth - row.gap)
        y: row.stacked ? 0 : Math.round((row.height - height) / 2)
        spacing: 2
        Txt { text: row.title; role: "label"; width: parent.width }
        Txt { visible: row.description !== ""; text: row.description; role: "caption"; width: parent.width }
    }
    Row {
        id: slot
        x: row.stacked ? 0 : row.width - implicitWidth
        y: row.stacked ? info.height + Theme.s2 : Math.round((row.height - height) / 2)
        spacing: Theme.s2
    }
}
