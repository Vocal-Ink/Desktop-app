import QtQuick
import QtQuick.Layouts

// Layout shared by the setup slides. Wide windows: the heading and why on the
// left, the controls on the right. Narrow windows: one column.
Item {
    id: step
    property string title
    property string lead: ""
    property Component visual: null     // optional illustration under the lead
    default property alias content: body.data
    // Steps can veto "Continue" (e.g. while a download runs) and say why.
    property bool canContinue: true
    property string waitingText: ""
    property string continueLabel: ""
    property Item firstFocus: null      // gets keyboard focus when the slide arrives
    function commit() {}

    readonly property bool wide: width >= Math.round(860 * Math.min(1.3, Theme.scale))
    readonly property real gutter: Math.round(64 * Math.min(1.3, Theme.scale))

    implicitHeight: wide ? Math.max(intro.implicitHeight, body.implicitHeight)
                         : intro.implicitHeight + Theme.s6 + body.implicitHeight

    ColumnLayout {
        id: intro
        width: step.wide ? Math.round(step.width * 0.4) : step.width
        spacing: Theme.s4

        Txt {
            Layout.fillWidth: true
            text: step.title
            role: "hero"
            font.pixelSize: Math.round(Theme.fsHero * (step.wide ? 0.98 : 0.86))
            lineHeight: 1.04
            Accessible.role: Accessible.Heading
        }
        // A short stroke of ink under the heading.
        Rectangle {
            Layout.preferredWidth: Math.round(56 * Theme.scale)
            Layout.preferredHeight: Math.round(5 * Math.min(1.4, Theme.scale))
            radius: height / 2
            color: Theme.accent
        }
        Txt {
            visible: step.lead !== ""
            Layout.fillWidth: true
            Layout.maximumWidth: Math.round(560 * Theme.scale)
            text: step.lead
            role: "lead"
            color: Theme.muted
        }
        Loader {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            active: step.visual !== null
            visible: active
            sourceComponent: step.visual
        }
    }

    ColumnLayout {
        id: body
        x: step.wide ? intro.width + step.gutter : 0
        y: step.wide ? 0 : intro.implicitHeight + Theme.s6
        width: step.wide ? step.width - intro.width - step.gutter : step.width
        spacing: Theme.s4
    }
}
