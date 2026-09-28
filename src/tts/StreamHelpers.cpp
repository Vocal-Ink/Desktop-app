#include "tts/StreamHelpers.h"

#include "audio/AudioConvert.h"
#include "core/NetworkUtil.h"

#include <QNetworkReply>
#include <QProcess>

// --- PcmStreamParser -----------------------------------------------------------

PcmStreamParser::PcmStreamParser(Container container, const QAudioFormat &rawFormat)
    : m_container(container)
{
    if (container == Container::Raw) {
        m_format = rawFormat;
        m_headerDone = true;
    }
}

QByteArray PcmStreamParser::feed(const QByteArray &bytes)
{
    if (!m_error.isEmpty())
        return {};
    m_pending.append(bytes);

    if (!m_headerDone) {
        const AudioConvert::WavHeader h = AudioConvert::parseWavHeader(m_pending);
        if (!h.valid) {
            if (!h.needMoreData)
                m_error = QObject::tr("Unexpected audio data (not a supported WAV stream)");
            else if (m_pending.size() > 64 * 1024)
                m_error = QObject::tr("Audio header too large");
            return {};
        }
        m_format = h.format;
        m_remaining = h.dataSize;
        m_pending.remove(0, h.dataOffset);
        m_headerDone = true;
    }

    const int frameBytes = m_format.bytesPerFrame();
    if (frameBytes <= 0) {
        m_error = QObject::tr("Invalid audio format");
        return {};
    }
    qsizetype usable = m_pending.size() - (m_pending.size() % frameBytes);
    if (m_remaining >= 0)
        usable = std::min<qsizetype>(usable, qsizetype(m_remaining));
    if (usable <= 0)
        return {};
    QByteArray out = m_pending.left(usable);
    m_pending.remove(0, usable);
    if (m_remaining >= 0) {
        m_remaining -= usable;
        if (m_remaining == 0)
            m_pending.clear(); // ignore trailing chunks (LIST etc.)
    }
    m_total += out.size();
    return out;
}

// --- NetworkTtsStream ------------------------------------------------------------

NetworkTtsStream::NetworkTtsStream(QNetworkReply *reply, PcmStreamParser::Container container,
                                   const QAudioFormat &rawFormat, const QString &providerName, QObject *parent)
    : TtsStream(parent)
    , m_reply(reply)
    , m_parser(container, rawFormat)
    , m_provider(providerName)
{
    reply->setParent(this);
    connect(reply, &QNetworkReply::readyRead, this, &NetworkTtsStream::onReadyRead);
    connect(reply, &QNetworkReply::finished, this, &NetworkTtsStream::onFinished);
}

NetworkTtsStream::~NetworkTtsStream()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

void NetworkTtsStream::onCancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

void NetworkTtsStream::onReadyRead()
{
    if (!m_reply)
        return;
    const int status = NetworkUtil::httpStatus(m_reply);
    const QByteArray chunk = m_reply->readAll();
    if (status >= 300 || m_reply->error() != QNetworkReply::NoError) {
        m_errorBody.append(chunk.left(8192 - m_errorBody.size()));
        return;
    }
    // Some providers answer errors with 200 + JSON; sniff the first bytes.
    if (m_parser.totalBytes() == 0 && !m_parser.hasError()) {
        const QByteArray head = chunk.left(1).trimmed();
        const QString type = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (type.contains(QLatin1String("json")) || type.startsWith(QLatin1String("text/"))
            || (head == "{" && chunk.contains("\"error"))) {
            m_errorBody.append(chunk);
            return;
        }
    }
    const QByteArray pcm = m_parser.feed(chunk);
    if (m_parser.hasError()) {
        m_reply->abort();
        return;
    }
    if (!pcm.isEmpty())
        deliverAudio(m_parser.format(), pcm);
}

void NetworkTtsStream::onFinished()
{
    if (!m_reply)
        return;
    onReadyRead();
    const int status = NetworkUtil::httpStatus(m_reply);
    if (m_parser.hasError()) {
        deliverFailed(m_provider + QStringLiteral(": ") + m_parser.error());
    } else if (m_reply->error() != QNetworkReply::NoError || status >= 300 || !m_errorBody.isEmpty()) {
        m_errorBody.append(m_reply->readAll());
        deliverFailed(NetworkUtil::describeError(m_reply, m_errorBody, m_provider));
    } else if (m_parser.totalBytes() == 0) {
        deliverFailed(tr("%1 returned no audio").arg(m_provider));
    } else {
        deliverFinished();
    }
    m_reply->deleteLater();
    m_reply = nullptr;
}

// --- ProcessTtsStream ------------------------------------------------------------

ProcessTtsStream::ProcessTtsStream(QProcess *process, PcmStreamParser::Container container,
                                   const QAudioFormat &rawFormat, const QString &engineName, QObject *parent)
    : TtsStream(parent)
    , m_process(process)
    , m_parser(container, rawFormat)
    , m_engine(engineName)
{
    process->setParent(this);
    connect(process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray pcm = m_parser.feed(m_process->readAllStandardOutput());
        if (m_parser.hasError()) {
            m_process->kill();
            return;
        }
        if (!pcm.isEmpty())
            deliverAudio(m_parser.format(), pcm);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr.append(m_process->readAllStandardError().right(4096));
        if (m_stderr.size() > 4096)
            m_stderr = m_stderr.right(4096);
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart)
            deliverFailed(tr("%1 could not be started: %2").arg(m_engine, m_process->errorString()));
    });
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const QByteArray pcm = m_parser.feed(m_process->readAllStandardOutput());
        if (!pcm.isEmpty())
            deliverAudio(m_parser.format(), pcm);
        if (m_parser.hasError()) {
            deliverFailed(m_engine + QStringLiteral(": ") + m_parser.error());
        } else if (status != QProcess::NormalExit || exitCode != 0) {
            QString msg = QString::fromUtf8(m_stderr).trimmed();
            if (msg.size() > 300)
                msg = msg.right(300);
            deliverFailed(tr("%1 stopped with an error (code %2)%3")
                              .arg(m_engine)
                              .arg(exitCode)
                              .arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg));
        } else if (m_parser.totalBytes() == 0) {
            deliverFailed(tr("%1 produced no audio").arg(m_engine));
        } else {
            deliverFinished();
        }
    });
}

ProcessTtsStream::~ProcessTtsStream()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

void ProcessTtsStream::onCancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
    }
}

// --- BufferTtsStream ------------------------------------------------------------

BufferTtsStream::BufferTtsStream(QObject *parent)
    : TtsStream(parent)
{
}
