#pragma once

#include "tts/TtsEngine.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QPointer>
#include <QString>

class QNetworkReply;
class QProcess;

// Turns an arbitrary byte stream (raw PCM or a streamed WAV file) into
// frame-aligned PCM chunks.
class PcmStreamParser
{
public:
    enum class Container { Raw, Wav };

    explicit PcmStreamParser(Container container, const QAudioFormat &rawFormat = {});

    // Returns PCM that is ready to play (possibly empty while a header is incomplete).
    QByteArray feed(const QByteArray &bytes);
    bool hasFormat() const { return m_format.isValid(); }
    QAudioFormat format() const { return m_format; }
    bool hasError() const { return !m_error.isEmpty(); }
    QString error() const { return m_error; }
    qint64 totalBytes() const { return m_total; }

private:
    Container m_container;
    QAudioFormat m_format;
    QByteArray m_pending; // header bytes or a partial frame
    bool m_headerDone = false;
    qint64 m_remaining = -1; // bytes left in the WAV data chunk; -1 = unknown
    qint64 m_total = 0;
    QString m_error;
};

// TtsStream fed by an HTTP response body.
class NetworkTtsStream : public TtsStream
{
    Q_OBJECT
public:
    NetworkTtsStream(QNetworkReply *reply, PcmStreamParser::Container container,
                     const QAudioFormat &rawFormat, const QString &providerName, QObject *parent = nullptr);
    ~NetworkTtsStream() override;

protected:
    void onCancel() override;

private:
    void onReadyRead();
    void onFinished();

    QPointer<QNetworkReply> m_reply;
    PcmStreamParser m_parser;
    QString m_provider;
    QByteArray m_errorBody;
};

// TtsStream fed by a child process's stdout (Piper, espeak-ng). The process is
// started by the engine; the stream owns it and kills it on cancel.
class ProcessTtsStream : public TtsStream
{
    Q_OBJECT
public:
    ProcessTtsStream(QProcess *process, PcmStreamParser::Container container,
                     const QAudioFormat &rawFormat, const QString &engineName, QObject *parent = nullptr);
    ~ProcessTtsStream() override;

protected:
    void onCancel() override;

private:
    QPointer<QProcess> m_process;
    PcmStreamParser m_parser;
    QString m_engine;
    QByteArray m_stderr;
};

// A stream whose audio is produced in one go (or already known).
class BufferTtsStream : public TtsStream
{
    Q_OBJECT
public:
    explicit BufferTtsStream(QObject *parent = nullptr);
    using TtsStream::deliverAudio;
    using TtsStream::deliverFailed;
    using TtsStream::deliverFailedLater;
    using TtsStream::deliverFinished;
};
