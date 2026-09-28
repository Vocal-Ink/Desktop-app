#include "tts/EspeakTtsEngine.h"

#include "tts/StreamHelpers.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

// Places installers use that are often missing from a GUI app's PATH.
QStringList extraSearchPaths()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("C:/Program Files/eSpeak NG"), QStringLiteral("C:/Program Files (x86)/eSpeak NG")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")};
#else
    return {};
#endif
}

// "en-gb-x-rp" -> "en-GB-x-rp"
QString toBcp47(const QString &espeakLanguage)
{
    QStringList parts = espeakLanguage.split(QLatin1Char('-'));
    for (int i = 1; i < parts.size(); ++i) {
        if (parts.at(i).size() == 1)
            break; // private use / extension subtags stay as they are
        if (parts.at(i).size() == 2)
            parts[i] = parts.at(i).toUpper();
    }
    return parts.join(QLatin1Char('-'));
}

} // namespace

EspeakTtsEngine::EspeakTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
}

QString EspeakTtsEngine::executablePath()
{
    for (const auto *name : {"espeak-ng", "espeak"}) {
        QString path = QStandardPaths::findExecutable(QLatin1String(name));
        if (path.isEmpty() && !extraSearchPaths().isEmpty())
            path = QStandardPaths::findExecutable(QLatin1String(name), extraSearchPaths());
        if (!path.isEmpty())
            return path;
    }
    return {};
}

bool EspeakTtsEngine::isAvailable() const
{
    return !executablePath().isEmpty();
}

QString EspeakTtsEngine::unavailableReason() const
{
    return isAvailable() ? QString() : tr("Install eSpeak NG (espeak-ng) to use these voices.");
}

QList<Voice> EspeakTtsEngine::parseVoiceList(const QByteArray &output)
{
    // Pty Language Age/Gender VoiceName File Other Languages
    //  5  en-us    --/M       English_(America) gmw/en-US (en 3)
    QList<Voice> list;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QStringList fields = QString::fromUtf8(raw).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 4 || fields.at(0) == QLatin1String("Pty"))
            continue;
        Voice v;
        v.engineId = QStringLiteral("espeak");
        v.id = fields.at(1);
        v.language = toBcp47(v.id);
        v.name = QString(fields.at(3)).replace(QLatin1Char('_'), QLatin1Char(' '));
        const QString gender = fields.at(2).section(QLatin1Char('/'), 1).trimmed();
        if (gender == QLatin1String("F"))
            v.gender = QStringLiteral("Female");
        else if (gender == QLatin1String("M"))
            v.gender = QStringLiteral("Male");
        if (v.id.isEmpty() || list.contains(v))
            continue;
        list << v;
    }
    std::sort(list.begin(), list.end(), [](const Voice &a, const Voice &b) { return a.id < b.id; });
    return list;
}

void EspeakTtsEngine::refreshVoices()
{
    const QString exe = executablePath();
    if (exe.isEmpty()) {
        setVoices({});
        return;
    }
    setRefreshing(true);
    const int generation = ++m_refreshGeneration;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, generation](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (generation != m_refreshGeneration)
            return;
        const QList<Voice> list = parseVoiceList(process->readAllStandardOutput());
        if (status != QProcess::NormalExit || exitCode != 0 || list.isEmpty()) {
            setRefreshing(false);
            emit voicesError(tr("eSpeak NG could not list its voices."));
            return;
        }
        setVoices(list);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        if (generation != m_refreshGeneration)
            return;
        setRefreshing(false);
        emit voicesError(tr("eSpeak NG could not be started: %1").arg(process->errorString()));
    });
    process->start(exe, {QStringLiteral("--voices")});
}

TtsStream *EspeakTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    const QString exe = executablePath();
    if (exe.isEmpty()) {
        auto *stream = new BufferTtsStream;
        stream->deliverFailedLater(unavailableReason());
        return stream;
    }
    const int wordsPerMinute = qRound(175.0 * qBound(0.5, options.rate, 2.0));
    const int pitch = qBound(0, qRound(50.0 + qBound(-1.0, options.pitch, 1.0) * 40.0), 99);
    const QStringList args = {
        QStringLiteral("--stdout"),
        QStringLiteral("-b"), QStringLiteral("1"), // UTF-8 input
        QStringLiteral("-v"), voice.id.isEmpty() ? QStringLiteral("en") : voice.id,
        QStringLiteral("-s"), QString::number(wordsPerMinute),
        QStringLiteral("-p"), QString::number(pitch),
        QStringLiteral("--stdin"),
    };
    auto *process = new QProcess;
    process->setProgram(exe);
    process->setArguments(args);
    auto *stream = new ProcessTtsStream(process, PcmStreamParser::Container::Wav, QAudioFormat(), displayName());

    const QByteArray input = text.toUtf8();
    // Started on the next event loop pass so the caller can connect first.
    QTimer::singleShot(0, stream, [stream, process, input] {
        if (stream->isCancelled())
            return;
        process->start();
        process->write(input);
        process->closeWriteChannel();
    });
    return stream;
}
