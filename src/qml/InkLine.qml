import QtQuick

// The signature: a line of text that fills with ink word by word as it's
// spoken. Words ahead are pale, the word being spoken is wet (accent) and
// fills left to right, spoken words are dry ink.
Item {
    id: root
    property string text: ""
    property real progress: 1        // 0..1 through the line
    property bool animate: true      // false: every word is already dry
    property real fontSize: Theme.stageSize
    property string fontFamily: Theme.stageFont
    property int fontWeight: Font.DemiBold
    property int horizontalAlignment: Qt.AlignLeft

    readonly property var words: text.split(/\s+/).filter(w => w.length > 0)
    // Where each word starts, by characters (long words take longer to say).
    readonly property var starts: {
        const out = []
        let total = 0
        for (let i = 0; i < words.length; ++i) {
            out.push(total)
            total += words[i].length + 1
        }
        out.push(Math.max(1, total))
        return out
    }
    readonly property real totalChars: starts.length ? starts[starts.length - 1] : 1

    implicitHeight: flow.implicitHeight
    implicitWidth: flow.implicitWidth
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    function fillOf(i) {
        if (!animate || !Theme.prefs["ui/inkEffect"])
            return 1
        const pos = progress * totalChars
        const a = starts[i], b = starts[i + 1] - 1
        if (pos <= a) return 0
        if (pos >= b) return 1
        return Theme.reducedMotion ? 1 : (pos - a) / Math.max(1, b - a)
    }

    Flow {
        id: flow
        width: root.width
        spacing: space.advanceWidth
        layoutDirection: Qt.LeftToRight
        flow: Flow.LeftToRight

        TextMetrics { id: space; text: " "; font.family: root.fontFamily; font.pixelSize: root.fontSize; font.weight: root.fontWeight }

        Repeater {
            model: root.words
            Item {
                id: word
                required property string modelData
                required property int index
                readonly property real fill: root.fillOf(index)
                width: base.implicitWidth
                height: base.implicitHeight

                Text {
                    id: base
                    text: word.modelData
                    font.family: root.fontFamily
                    font.pixelSize: root.fontSize
                    font.weight: root.fontWeight
                    font.letterSpacing: Theme.tracking(root.fontSize)
                    color: word.fill >= 1 ? Theme.inkDry : Theme.inkUnwritten
                    Behavior on color { ColorAnimation { duration: Theme.normal } }
                }
                // Wet ink, clipped to how much of the word has been said.
                Item {
                    visible: word.fill > 0 && word.fill < 1
                    clip: true
                    width: base.width * word.fill
                    height: base.height
                    Text {
                        text: word.modelData
                        font: base.font
                        color: Theme.inkWet
                    }
                }
            }
        }
    }
}
