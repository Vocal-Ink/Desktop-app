#include "audio/MicCapture.h"

#include "audio/AudioConvert.h"
#include "audio/Resampler.h"

#include <QAudioSource>
#include <QMediaDevices>
#include <QtMath>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QCoreApplication>
#include <QPermissions>
#endif

class MicCapture::Private
{
public:
    QByteArray deviceId;
    QAudioDevice device;
    QAudioFormat format;
    QAudioSource *source = nullptr;
    QIODevice *io = nullptr;
    Resampler resampler;
    QByteArray leftover;
    int outputRate = MicCapture::SampleRate;
    int bufferMs = 100;
};

MicCapture::MicCapture(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

MicCapture::~MicCapture()
{
    stop();
    delete d;
}

void MicCapture::setDevice(const QByteArray &deviceId)
{
    if (d->deviceId == deviceId)
        return;
    d->deviceId = deviceId;
    if (isRunning()) {
        stop();
        start();
    }
}

void MicCapture::setOutputRate(int rate)
{
    if (rate > 0)
        d->outputRate = rate;
}

int MicCapture::outputRate() const
{
    return d->outputRate;
}

void MicCapture::setBufferDuration(int ms)
{
    d->bufferMs = qBound(5, ms, 1000);
}

bool MicCapture::isRunning() const
{
    return d->source && d->source->state() != QAudio::StoppedState;
}

QString MicCapture::deviceName() const
{
    return d->device.description();
}

bool MicCapture::start()
{
    if (isRunning())
        return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // macOS (and some sandboxed setups) require explicit microphone permission.
    QMicrophonePermission permission;
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted)
                start();
            else
                emit errorOccurred(tr("Microphone access was denied. Allow Vocal Ink in your system's privacy settings."));
        });
        return false;
    case Qt::PermissionStatus::Denied:
        emit errorOccurred(tr("Microphone access was denied. Allow Vocal Ink in your system's privacy settings."));
        return false;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif

    d->device = QMediaDevices::defaultAudioInput();
    if (!d->deviceId.isEmpty()) {
        const auto inputs = QMediaDevices::audioInputs();
        for (const QAudioDevice &dev : inputs) {
            if (dev.id() == d->deviceId) {
                d->device = dev;
                break;
            }
        }
    }
    if (d->device.isNull()) {
        emit errorOccurred(tr("No microphone was found."));
        return false;
    }

    // Ask for the output rate in mono directly when the device can do it;
    // otherwise take its preferred format and convert.
    QAudioFormat want;
    want.setSampleRate(d->outputRate);
    want.setChannelCount(1);
    want.setSampleFormat(QAudioFormat::Int16);
    d->format = d->device.isFormatSupported(want) ? want : d->device.preferredFormat();
    d->resampler.reset(d->format.sampleRate(), d->outputRate);
    d->leftover.clear();

    delete d->source;
    d->source = new QAudioSource(d->device, d->format, this);
    d->source->setBufferSize(d->format.bytesForDuration(qint64(d->bufferMs) * 1000));
    d->io = d->source->start();
    if (!d->io || d->source->error() != QAudio::NoError) {
        emit errorOccurred(tr("Could not open the microphone \"%1\".").arg(d->device.description()));
        delete d->source;
        d->source = nullptr;
        d->io = nullptr;
        return false;
    }
    connect(d->io, &QIODevice::readyRead, this, [this] {
        if (!d->io)
            return;
        QByteArray data = d->leftover + d->io->readAll();
        const int frame = d->format.bytesPerFrame();
        if (frame <= 0)
            return;
        const qsizetype usable = data.size() - data.size() % frame;
        d->leftover = data.mid(usable);
        data.truncate(usable);
        if (data.isEmpty())
            return;
        const QVector<float> mono = AudioConvert::toMonoFloat(data, d->format);
        QVector<float> out = d->resampler.isPassthrough() ? mono : d->resampler.process(mono);
        if (out.isEmpty())
            return;
        // Perceptual-ish level: map -60..0 dBFS to 0..1.
        const float r = AudioConvert::rms(out.constData(), out.size());
        const float db = 20.0f * std::log10(r + 1e-9f);
        emit levelChanged(qBound(0.0f, (db + 60.0f) / 60.0f, 1.0f));
        emit samples(out);
    });
    return true;
}

void MicCapture::stop()
{
    if (d->source) {
        d->source->stop();
        d->source->deleteLater();
        d->source = nullptr;
    }
    d->io = nullptr;
    d->leftover.clear();
    emit levelChanged(0.0f);
}
