#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// Speech-to-text backend. Audio is always 16 kHz mono float in [-1, 1].
class SttEngine : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString id() const = 0;          // "whisper" | "openai"
    virtual QString displayName() const = 0;
    virtual bool isLocal() const = 0;
    virtual bool isReady() const = 0;        // model loaded / key present
    virtual QString notReadyReason() const { return {}; }

    struct Options
    {
        QString language = QStringLiteral("auto"); // "auto" or ISO-639-1 code
        QString prompt;                              // custom vocabulary, names, slang
    };
    void setOptions(const Options &options) { m_options = options; }
    Options options() const { return m_options; }

    // Asynchronous; answers with transcribed() or failed() carrying the same requestId.
    virtual void transcribe(quint64 requestId, const QVector<float> &mono16k) = 0;

signals:
    void transcribed(quint64 requestId, const QString &text);
    void failed(quint64 requestId, const QString &error);
    void readyChanged(bool ready);

protected:
    Options m_options;
};
