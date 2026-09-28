import QtQuick
import QtQuick.Layouts

// Layout shared by every onboarding step: a heading, one line of why, then
// the controls.
ColumnLayout {
    id: step
    property string title
    property string lead: ""
    default property alias content: body.data
    // Steps can veto "Continue" (e.g. while a download runs) and say why.
    property bool canContinue: true
    property string continueLabel: ""
    function commit() {}

    spacing: Theme.s5

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        Txt {
            Layout.fillWidth: true
            text: step.title
            role: "hero"
            font.pixelSize: Math.round(Theme.fsHero * 0.92)
            Accessible.role: Accessible.Heading
        }
        Txt {
            visible: step.lead !== ""
            Layout.fillWidth: true
            Layout.maximumWidth: Math.round(620 * Theme.scale)
            text: step.lead
            role: "lead"
            color: Theme.muted
        }
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.s4
    }
}
