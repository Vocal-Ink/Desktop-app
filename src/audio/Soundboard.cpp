#include "audio/Soundboard.h"

#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "audio/Resampler.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <memory>

namespace {
constexpr int kStoreVersion = 1;
constexpr int kMaxSeconds = 600; // longer files are cut; this is a soundboard, not a music player
constexpr int kMaxIdLength = 40;
constexpr int kDecodeStallMs = 10000; // some backends never report junk input

QString storeFile(const QString &dir)
{
    return QDir(dir).filePath(QStringLiteral("sounds.json"));
}
} // namespace

class Soundboard::Private
{
public:
    struct Decoded
    {
        QVector<float> mono;
        int rate = 0;
    };
    QHash<QString, Decoded> cache;
    QHash<QString, quint64> handles; // sound id -> AudioPlayer sound id
    QHash<quint64, QString> ids;
    QSet<QString> playing;
    QHash<QString, QAudioDecoder *> decoders;
    QSet<QString> playWhenReady;
    quint64 nextHandle = 1;
};

Soundboard::Soundboard(AudioPlayer *player, const QString &storeDir, QObject *parent)
    : QObject(parent)
    , d(new Private)
    , m_player(player)
    , m_dir(storeDir)
{
    if (m_player) {
        connect(m_player, &AudioPlayer::soundFinished, this, [this](quint64 handle) {
            const QString id = d->ids.value(handle);
            if (!id.isEmpty() && d->playing.remove(id))
                emit playingChanged(id, false);
        });
    }
}

Soundboard::~Soundboard()
{
    for (QAudioDecoder *decoder : std::as_const(d->decoders)) {
        decoder->disconnect(this);
        delete decoder;
    }
    delete d;
}

bool Soundboard::load()
{
    m_sounds.clear();
    d->cache.clear();
    QFile f(storeFile(m_dir));
    if (!f.exists()) {
        emit changed();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QDir dir(m_dir);
    const QJsonArray list = doc.object().value(QLatin1String("sounds")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject o = value.toObject();
        Sound s;
        s.id = o.value(QLatin1String("id")).toString();
        if (s.id.isEmpty() || indexOf(s.id) >= 0)
            continue;
        s.name = o.value(QLatin1String("name")).toString(s.id);
        const QString file = o.value(QLatin1String("file")).toString();
        s.file = QFileInfo(file).isAbsolute() ? file : dir.filePath(file);
        s.hotkey = o.value(QLatin1String("hotkey")).toString();
        s.gain = float(o.value(QLatin1String("gain")).toDouble(1.0));
        s.color = o.value(QLatin1String("color")).toString();
        m_sounds.append(s);
    }
    emit changed();
    return true;
}

bool Soundboard::save() const
{
    if (!QDir().mkpath(m_dir))
        return false;
    const QDir dir(m_dir);
    QJsonArray list;
    for (const Sound &s : m_sounds) {
        // Files inside the store are saved relative to it, so the folder can move.
        QString file = s.file;
        if (isInsideStore(s.file))
            file = dir.relativeFilePath(s.file);
        list.append(QJsonObject{{QStringLiteral("id"), s.id},
                                {QStringLiteral("name"), s.name},
                                {QStringLiteral("file"), file},
                                {QStringLiteral("hotkey"), s.hotkey},
                                {QStringLiteral("gain"), double(s.gain)},
                                {QStringLiteral("color"), s.color}});
    }
    const QJsonObject root{{QStringLiteral("version"), kStoreVersion}, {QStringLiteral("sounds"), list}};
    QSaveFile f(storeFile(m_dir));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson());
    return f.commit();
}

int Soundboard::indexOf(const QString &id) const
{
    for (int i = 0; i < m_sounds.size(); ++i) {
        if (m_sounds.at(i).id == id)
            return i;
    }
    return -1;
}

bool Soundboard::isInsideStore(const QString &file) const
{
    const QString store = QDir(m_dir).canonicalPath();
    return !store.isEmpty() && QFileInfo(file).canonicalPath() == store;
}

QString Soundboard::addFile(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };
    const QFileInfo source(path);
    if (!source.isFile())
        return fail(tr("“%1” doesn't exist.").arg(source.fileName()));
    const QString ext = source.suffix().toLower();
    if (!supportedExtensions().contains(ext)) {
        return fail(tr("“%1” isn't a sound file Vocal Ink can play. Use %2.")
                        .arg(source.fileName(), supportedExtensions().join(QStringLiteral(", "))));
    }
    if (!QDir().mkpath(m_dir))
        return fail(tr("Couldn't create the soundboard folder “%1”.").arg(QDir::toNativeSeparators(m_dir)));

    // The id doubles as the file name: lower-case ASCII, unique in the store.
    static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9]+"));
    QString base = source.completeBaseName().toLower();
    base.replace(unsafe, QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    base.truncate(kMaxIdLength);
    if (base.isEmpty())
        base = QStringLiteral("sound");
    const QDir dir(m_dir);
    QString id = base;
    for (int n = 2; indexOf(id) >= 0 || QFileInfo::exists(dir.filePath(id + QLatin1Char('.') + ext)); ++n)
        id = base + QLatin1Char('-') + QString::number(n);

    const QString dest = dir.filePath(id + QLatin1Char('.') + ext);
    if (!QFile::copy(path, dest))
        return fail(tr("Couldn't copy “%1” into the soundboard folder.").arg(source.fileName()));
    // A copy of a read-only file must still be removable later.
    QFile::setPermissions(dest, QFile::permissions(dest) | QFileDevice::WriteOwner | QFileDevice::ReadOwner);

    Sound s;
    s.id = id;
    s.name = source.completeBaseName();
    s.file = dest;
    m_sounds.append(s);
    save();
    emit changed();
    if (error)
        error->clear();
    return id;
}

void Soundboard::update(const Sound &sound)
{
    const int i = indexOf(sound.id);
    if (i < 0)
        return;
    Sound s = sound;
    if (s.file.isEmpty())
        s.file = m_sounds.at(i).file;
    if (s.file != m_sounds.at(i).file) {
        cancelDecoder(s.id);
        d->cache.remove(s.id);
    }
    s.gain = std::clamp(s.gain, 0.0f, 4.0f);
    m_sounds[i] = s;
    save();
    emit changed();
}

void Soundboard::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    stop(id);
    cancelDecoder(id);
    const Sound s = m_sounds.takeAt(i);
    d->cache.remove(id);
    if (isInsideStore(s.file)) // never delete a file outside our own folder
        QFile::remove(s.file);
    save();
    emit changed();
}

void Soundboard::move(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_sounds.size() || to >= m_sounds.size())
        return;
    m_sounds.move(from, to);
    save();
    emit changed();
}

void Soundboard::setDecodedAudio(const QString &id, const QVector<float> &mono, int sampleRate)
{
    cancelDecoder(id);
    d->cache.insert(id, Private::Decoded{mono, sampleRate});
}

bool Soundboard::decodeWav(const QByteArray &bytes, QVector<float> *mono, int *sampleRate, QString *error)
{
    const AudioConvert::WavHeader h = AudioConvert::parseWavHeader(bytes);
    if (!h.valid) {
        if (error)
            *error = h.needMoreData ? tr("the file is cut short") : tr("this kind of WAV file isn't supported");
        return false;
    }
    qint64 size = bytes.size() - h.dataOffset;
    if (h.dataSize >= 0)
        size = std::min(size, h.dataSize);
    size = std::min(size, qint64(kMaxSeconds) * h.format.sampleRate() * h.format.bytesPerFrame());
    const QVector<float> out = AudioConvert::toMonoFloat(bytes.constData() + h.dataOffset, qsizetype(size), h.format);
    if (out.isEmpty() || h.format.sampleRate() <= 0) {
        if (error)
            *error = tr("it contains no audio");
        return false;
    }
    if (mono)
        *mono = out;
    if (sampleRate)
        *sampleRate = h.format.sampleRate();
    return true;
}

void Soundboard::play(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    if (d->cache.contains(id)) {
        startPlayback(id);
        return;
    }
    if (d->decoders.contains(id)) {
        d->playWhenReady.insert(id);
        return;
    }
    const QString file = m_sounds.at(i).file;
    const QString name = QFileInfo(file).fileName();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Couldn't read %1: %2").arg(name, f.exists() ? f.errorString() : tr("the file is missing")));
        return;
    }
    // WAV is decoded right here: instant, and needs no media backend.
    if (f.peek(4) == "RIFF") {
        const QByteArray bytes = f.readAll();
        Private::Decoded decoded;
        QString error;
        if (decodeWav(bytes, &decoded.mono, &decoded.rate, &error)) {
            d->cache.insert(id, decoded);
            startPlayback(id);
            return;
        }
        // Unusual encodings (24-bit, ADPCM...) may still work with the decoder.
    }
    f.close();
    d->playWhenReady.insert(id);
    startDecoder(id, file);
}

void Soundboard::startPlayback(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0 || !d->cache.contains(id))
        return;
    const Private::Decoded decoded = d->cache.value(id);
    quint64 &handle = d->handles[id];
    if (handle == 0) {
        handle = d->nextHandle++;
        d->ids.insert(handle, id);
    }
    const bool wasPlaying = d->playing.contains(id);
    d->playing.insert(id);
    if (m_player)
        m_player->playSound(handle, decoded.mono, decoded.rate, m_sounds.at(i).gain); // restarts if playing
    if (!wasPlaying)
        emit playingChanged(id, true);
}

void Soundboard::startDecoder(const QString &id, const QString &file)
{
    const QString name = QFileInfo(file).fileName();
    auto *decoder = new QAudioDecoder(this);
    if (!decoder->isSupported()) {
        delete decoder;
        d->playWhenReady.remove(id);
        emit errorOccurred(tr("Couldn't read %1: this system can't decode it. Convert it to WAV and add it again.")
                               .arg(name));
        return;
    }
    d->decoders.insert(id, decoder);
    auto result = std::make_shared<Private::Decoded>();
    auto *watchdog = new QTimer(decoder);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this, id, decoder, name] {
        if (d->decoders.value(id) != decoder)
            return;
        cancelDecoder(id);
        emit errorOccurred(tr("Couldn't read %1: %2").arg(name, tr("it doesn't look like a sound file")));
    });
    watchdog->start(kDecodeStallMs);
    const auto drain = [decoder, result, watchdog] {
        watchdog->start(kDecodeStallMs);
        while (decoder->bufferAvailable()) {
            const QAudioBuffer buffer = decoder->read();
            if (!buffer.isValid())
                continue;
            const QAudioFormat format = buffer.format();
            QVector<float> mono = AudioConvert::toMonoFloat(buffer.constData<char>(), buffer.byteCount(), format);
            if (result->rate <= 0)
                result->rate = format.sampleRate();
            else if (format.sampleRate() != result->rate)
                mono = Resampler::convert(mono, format.sampleRate(), result->rate);
            if (result->mono.size() < qsizetype(kMaxSeconds) * result->rate)
                result->mono += mono;
        }
    };
    connect(decoder, &QAudioDecoder::bufferReady, this, drain);
    connect(decoder, &QAudioDecoder::finished, this, [this, id, decoder, result, drain, name] {
        if (d->decoders.value(id) != decoder)
            return;
        drain();
        d->decoders.remove(id);
        decoder->deleteLater();
        const bool wanted = d->playWhenReady.remove(id);
        if (result->mono.isEmpty() || result->rate <= 0) {
            emit errorOccurred(tr("Couldn't read %1: %2").arg(name, tr("it contains no audio")));
            return;
        }
        d->cache.insert(id, *result);
        if (wanted)
            startPlayback(id);
    });
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
            [this, id, decoder, name](QAudioDecoder::Error) {
                if (d->decoders.value(id) != decoder)
                    return;
                d->decoders.remove(id);
                d->playWhenReady.remove(id);
                QString reason = decoder->errorString();
                if (reason.isEmpty())
                    reason = tr("the format isn't supported");
                decoder->deleteLater();
                emit errorOccurred(tr("Couldn't read %1: %2").arg(name, reason));
            });
    decoder->setSource(QUrl::fromLocalFile(file));
    decoder->start();
}

void Soundboard::cancelDecoder(const QString &id)
{
    d->playWhenReady.remove(id);
    QAudioDecoder *decoder = d->decoders.take(id);
    if (!decoder)
        return;
    decoder->disconnect(this);
    decoder->stop();
    decoder->deleteLater();
}

void Soundboard::stop(const QString &id)
{
    d->playWhenReady.remove(id);
    const quint64 handle = d->handles.value(id);
    if (handle && m_player)
        m_player->stopSound(handle); // soundFinished -> playingChanged(false)
    if (d->playing.remove(id))
        emit playingChanged(id, false);
}

void Soundboard::stopAll()
{
    d->playWhenReady.clear();
    const QList<QString> ids = d->playing.values();
    for (const QString &id : ids)
        stop(id);
}

bool Soundboard::isPlaying(const QString &id) const
{
    return d->playing.contains(id);
}

QStringList Soundboard::supportedExtensions()
{
    return {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("flac"),
            QStringLiteral("m4a"), QStringLiteral("opus")};
}
