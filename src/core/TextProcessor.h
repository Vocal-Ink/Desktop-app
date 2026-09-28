#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Pure text helpers used between the keyboard/recognizer and the voice.
namespace TextProcessor {

// Replaces whole-word abbreviations (case-insensitive), e.g. "brb" -> "be right back".
// If the abbreviation was typed with a leading capital, the expansion is capitalised.
QString expandReplacements(const QString &text, const QVariantMap &replacements);

// Splits text into chunks that can be synthesised independently, so the first
// sentence can start playing while the rest is generated. Short sentences are
// merged; chunks never exceed maxChars unless a single word is longer.
QStringList splitForSpeech(const QString &text, int minChars = 40, int maxChars = 300);

// Cleans up a speech-recognition transcript: drops non-speech annotations like
// "[BLANK_AUDIO]", "(music)" or "♪", collapses whitespace, trims.
QString cleanTranscript(const QString &text);

// Collapses whitespace and removes control characters; returns empty for
// input that has nothing speakable in it.
QString normalizeForSpeech(const QString &text);

// Words for auto-completion, extracted from free text (lowercase, >= 3 letters).
QStringList vocabularyFrom(const QString &text);

// --- Message preparation (applied in this order before speaking) --------------
struct VariableContext
{
    QVariantMap custom;  // user-defined {name} -> value
    QString clipboard;
    QString voiceName;
    QDateTime now;       // invalid = current time
};
// Replaces {time}, {date}, {day}, {clipboard}, {voice} and custom {names}
// (case-insensitive; custom names win). Unknown {tokens} are left as typed;
// "{{" and "}}" are literal braces.
QString expandVariables(const QString &text, const VariableContext &context);

enum class EmojiMode { Speak, Remove, Keep };
// Speak: common emoji become words ("😂" -> "laughing", repeats read once, others
// dropped); Remove strips them, including skin tones and joined sequences.
QString handleEmoji(const QString &text, EmojiMode mode);

enum class UrlMode { Keep, SayLink, Remove };
// SayLink: "https://example.com/x" -> "link"; also handles bare www. links.
QString handleUrls(const QString &text, UrlMode mode);

// Capitalises the first letter of each sentence ("hi. ok" -> "Hi. Ok") and a
// standalone "i" ("i'm" -> "I'm"); nothing else changes.
QString autoCapitalize(const QString &text);

} // namespace TextProcessor
