#include "tts/PiperTtsEngine.h"

#include "audio/AudioConvert.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "tts/StreamHelpers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

// "libritts_r" -> "Libritts r"
QString prettify(QString name)
{
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!name.isEmpty())
        name[0] = name.at(0).toUpper();
    return name;
}

TtsStream *failedStream(const QString &message)
{
    auto *stream = new BufferTtsStream;
    stream->deliverFailedLater(message);
    return stream;
}

} // namespace

PiperTtsEngine::PiperTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
    m_voices = scan();
    m_wasAvailable = isAvailable();
}

QString PiperTtsEngine::executablePath() const
{
    const QString configured = m_ctx.settings ? m_ctx.settings->string(Keys::PiperExecutable).trimmed() : QString();
    if (!configured.isEmpty() && QFileInfo(configured).isFile())
        return QFileInfo(configured).absoluteFilePath();
#ifdef Q_OS_WIN
    const QString bundled = Paths::piperRuntimeDir() + QStringLiteral("/piper/piper.exe");
#else
    const QString bundled = Paths::piperRuntimeDir() + QStringLiteral("/piper/piper");
#endif
    if (QFileInfo(bundled).isFile())
        return bundled;
    return QStandardPaths::findExecutable(QStringLiteral("piper"));
}

bool PiperTtsEngine::isAvailable() const
{
    if (executablePath().isEmpty())
        return false;
    // Also looks at the disk so a freshly downloaded first voice counts before a refresh.
    return !m_voices.isEmpty() || !findModelFiles(Paths::piperVoicesDir(), 1).isEmpty();
}

QString PiperTtsEngine::unavailableReason() const
{
    if (executablePath().isEmpty())
        return tr("Download the Piper voice engine in Settings → Voices.");
    if (!isAvailable())
        return tr("Download a Piper voice in Settings → Voices.");
    return {};
}

QStringList PiperTtsEngine::findModelFiles(const QString &dir, int depth)
{
    QStringList found;
    const QDir d(dir);
    const QFileInfoList files = d.entryInfoList({QStringLiteral("*.onnx")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (QFileInfo::exists(fi.absoluteFilePath() + QStringLiteral(".json")))
            found << fi.absoluteFilePath();
    }
    if (depth < 2) {
        const QFileInfoList dirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &sub : dirs)
            found += findModelFiles(sub.absoluteFilePath(), depth + 1);
    }
    return found;
}

QList<Voice> PiperTtsEngine::scan()
{
    m_models.clear();
    QList<Voice> list;
    const QStringList files = findModelFiles(Paths::piperVoicesDir(), 1);
    for (const QString &onnx : files) {
        const QString key = QFileInfo(onnx).completeBaseName();
        if (key.isEmpty() || m_models.contains(key))
            continue;
        QFile file(onnx + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;
        const QJsonObject root = doc.object();
        const QJsonObject audio = root.value(QStringLiteral("audio")).toObject();
        const QJsonObject language = root.value(QStringLiteral("language")).toObject();

        Model model;
        model.onnxPath = onnx;
        model.configPath = onnx + QStringLiteral(".json");
        model.sampleRate = audio.value(QStringLiteral("sample_rate")).toInt(22050);
        if (model.sampleRate <= 0)
            model.sampleRate = 22050;
        model.speakerCount = qMax(1, root.value(QStringLiteral("num_speakers")).toInt(1));
        const QJsonObject speakerMap = root.value(QStringLiteral("speaker_id_map")).toObject();
        for (auto it = speakerMap.constBegin(); it != speakerMap.constEnd(); ++it)
            model.speakers.insert(it.key(), it.value().toInt());

        // Keys look like "en_US-lessac-medium": language, dataset, quality.
        Voice voice;
        voice.engineId = id();
        voice.id = key;
        QString code = language.value(QStringLiteral("code")).toString();
        if (code.isEmpty())
            code = key.section(QLatin1Char('-'), 0, 0);
        voice.language = code.replace(QLatin1Char('_'), QLatin1Char('-'));
        QString dataset = root.value(QStringLiteral("dataset")).toString();
        if (dataset.isEmpty())
            dataset = key.section(QLatin1Char('-'), 1, 1);
        QString quality = audio.value(QStringLiteral("quality")).toString();
        if (quality.isEmpty())
            quality = key.section(QLatin1Char('-'), 2, 2);
        voice.name = dataset.isEmpty() ? key : prettify(dataset);
        if (!quality.isEmpty())
            voice.name += QStringLiteral(" (%1)").arg(quality);
        const QString languageName = language.value(QStringLiteral("name_english")).toString();
        const QString country = language.value(QStringLiteral("country_english")).toString();
        voice.description = (languageName.isEmpty() || country.isEmpty())
            ? languageName
            : QStringLiteral("%1 (%2)").arg(languageName, country);

        m_models.insert(key, model);
        if (model.speakerCount <= 1) {
            list << voice;
            continue;
        }
        QList<QPair<int, QString>> speakers;
        for (auto it = model.speakers.cbegin(); it != model.speakers.cend(); ++it)
            speakers.append({it.value(), it.key()});
        if (speakers.isEmpty()) {
            for (int i = 0; i < model.speakerCount; ++i)
                speakers.append({i, QString::number(i)});
        }
        std::sort(speakers.begin(), speakers.end());
        for (const auto &speaker : std::as_const(speakers)) {
            Voice v = voice;
            v.id = key + QLatin1Char('#') + speaker.second;
            v.name = voice.name + QStringLiteral(" · ") + speaker.second;
            v.speaker = speaker.first;
            list << v;
        }
    }
    return list;
}

void PiperTtsEngine::refreshVoices()
{
    setVoices(scan());
    const bool available = isAvailable();
    if (available != m_wasAvailable) {
        m_wasAvailable = available;
        emit availabilityChanged();
    }
}

TtsStream *PiperTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    const QString exe = executablePath();
    if (exe.isEmpty())
        return failedStream(unavailableReason());

    // Voice ids are "<key>" or "<key>#<speaker name>" for multi-speaker models.
    const QString key = voice.id.section(QLatin1Char('#'), 0, 0);
    const QString speakerName = voice.id.contains(QLatin1Char('#')) ? voice.id.section(QLatin1Char('#'), 1) : QString();
    if (!m_models.contains(key)) {
        // Installed after the last refresh? Look again and publish the new list later.
        const QList<Voice> fresh = scan();
        QTimer::singleShot(0, this, [this, fresh] { setVoices(fresh); });
    }
    const auto it = m_models.constFind(key);
    if (it == m_models.constEnd())
        return failedStream(tr("The Piper voice \"%1\" is not installed. Download it in Settings → Voices.").arg(key));
    const Model &model = it.value();

    int speaker = voice.speaker;
    if (speaker < 0 && !speakerName.isEmpty()) {
        speaker = model.speakers.value(speakerName, -1);
        bool isNumber = false;
        const int number = speakerName.toInt(&isNumber);
        if (speaker < 0 && isNumber)
            speaker = number;
    }

    QStringList args = {
        QStringLiteral("--model"), model.onnxPath,
        QStringLiteral("--config"), model.configPath,
        QStringLiteral("--output_raw"),
        QStringLiteral("--quiet"),
    };
    if (model.speakerCount > 1 && speaker >= 0)
        args << QStringLiteral("--speaker") << QString::number(speaker);
    args << QStringLiteral("--length_scale") << QString::number(1.0 / qBound(0.5, options.rate, 2.0), 'f', 3);

    auto *process = new QProcess;
    process->setProgram(exe);
    process->setArguments(args);
    // piper looks for espeak-ng-data and its libraries next to itself.
    process->setWorkingDirectory(QFileInfo(exe).absolutePath());
    auto *stream = new ProcessTtsStream(process, PcmStreamParser::Container::Raw,
                                        AudioConvert::int16Mono(model.sampleRate), displayName());

    // piper reads one utterance per line.
    const QByteArray input = text.simplified().toUtf8() + '\n';
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
