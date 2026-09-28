#pragma once

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

} // namespace TextProcessor
