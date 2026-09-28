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

    readonly property bool leftAligned: horizontalAlignment === Qt.AlignLeft
    implicitHeight: leftAligned ? flow.implicitHeight : rows.implicitHeight
    implicitWidth: leftAligned ? flow.implicitWidth : rows.implicitWidth
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

    component Word: Item {
        id: word
        property string wordText
        property int wordIndex
        readonly property real fill: root.fillOf(wordIndex)
        width: base.implicitWidth
        height: base.implicitHeight

        Text {
            id: base
            text: word.wordText
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
                text: word.wordText
                font: base.font
                color: Theme.inkWet
            }
        }
    }

    TextMetrics { id: space; text: " "; font.family: root.fontFamily; font.pixelSize: root.fontSize; font.weight: root.fontWeight }

    // Left-aligned: a plain Flow.
    Flow {
        id: flow
        visible: root.leftAligned
        width: root.width
        spacing: space.advanceWidth
        layoutDirection: Qt.LeftToRight
        flow: Flow.LeftToRight

        Repeater {
            model: root.leftAligned ? root.words : []
            Word {
                required property string modelData
                required property int index
                wordText: modelData
                wordIndex: index
            }
        }
    }

    // Centred or right-aligned: break the words into lines here, then place
    // each line (Flow can only fill from the left).
    FontMetrics { id: metrics; font.family: root.fontFamily; font.pixelSize: root.fontSize; font.weight: root.fontWeight }
    readonly property var lines: {
        if (leftAligned || width <= 0)
            return []
        const out = []
        let line = [], used = 0
        const gap = space.advanceWidth
        const track = Theme.tracking(fontSize)
        for (let i = 0; i < words.length; ++i) {
            const w = metrics.advanceWidth(words[i]) + track * words[i].length
            if (line.length > 0 && used + gap + w > width) {
                out.push(line)
                line = []
                used = 0
            }
            used += (line.length > 0 ? gap : 0) + w
            line.push(i)
        }
        if (line.length > 0)
            out.push(line)
        return out
    }
    Column {
        id: rows
        visible: !root.leftAligned
        width: root.width
        spacing: space.advanceWidth // same line gap as the Flow
        Repeater {
            model: root.lines
            Row {
                required property var modelData
                spacing: space.advanceWidth
                x: root.horizontalAlignment === Qt.AlignRight ? rows.width - implicitWidth
                   : Math.max(0, (rows.width - implicitWidth) / 2)
                Repeater {
                    model: parent.modelData
                    Word {
                        required property int modelData
                        wordIndex: modelData
                        wordText: root.words[modelData] || ""
                    }
                }
            }
        }
    }
}
