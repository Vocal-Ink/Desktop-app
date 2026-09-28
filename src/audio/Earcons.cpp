#include "audio/Earcons.h"

#include "audio/AudioOutputLane.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

struct Note
{
    double freq;
    double startMs;
    double lengthMs;
    float level = 1.0f;
    bool bell = false; // exponential decay with a soft overtone (chimes)
};

// Sine with a touch of third harmonic for warmth; raised-cosine attack and a
// smooth release so nothing clicks.
void addNote(QVector<float> &out, int rate, const Note &n)
{
    const qsizetype start = qsizetype(n.startMs * rate / 1000.0);
    const qsizetype length = qsizetype(n.lengthMs * rate / 1000.0);
    const qsizetype attack = std::max<qsizetype>(1, qsizetype(0.006 * rate));
    const qsizetype release = std::max<qsizetype>(1, std::min(length / 2, qsizetype(0.045 * rate)));
    for (qsizetype i = 0; i < length && start + i < out.size(); ++i) {
        const double t = double(i) / rate;
        double env = 1.0;
        if (i < attack)
            env = 0.5 - 0.5 * std::cos(kPi * double(i) / double(attack));
        if (n.bell)
            env *= std::exp(-t * 9.0);
        const qsizetype toEnd = length - i;
        if (toEnd < release)
            env *= 0.5 - 0.5 * std::cos(kPi * double(toEnd) / double(release));
        const double w = 2.0 * kPi * n.freq * t;
        double v = std::sin(w) + 0.12 * std::sin(3.0 * w);
        if (n.bell)
            v = std::sin(w) + 0.35 * std::sin(2.76 * w) * std::exp(-t * 14.0);
        out[start + i] += float(0.3 * n.level * env * v);
    }
}

QVector<float> render(int rate, double totalMs, std::initializer_list<Note> notes)
{
    QVector<float> out(qsizetype(totalMs * rate / 1000.0), 0.0f);
    for (const Note &n : notes)
        addNote(out, rate, n);
    return out;
}

} // namespace

class Earcons::Private
{
public:
    QByteArray deviceId;
    float volume = 0.6f;
    AudioOutputLane *lane = nullptr;
    QHash<int, QVector<float>> cache;
    quint64 serial = 0;
};

Earcons::Earcons(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

Earcons::~Earcons()
{
    delete d->lane;
    delete d;
}

void Earcons::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Earcons::setDevice(const QByteArray &deviceId)
{
    if (deviceId == d->deviceId)
        return;
    d->deviceId = deviceId;
    if (d->lane) {
        d->lane->deleteLater();
        d->lane = nullptr;
    }
}

void Earcons::setVolume(float volume)
{
    d->volume = std::clamp(volume, 0.0f, 1.0f);
}

void Earcons::play(Cue cue)
{
    if (!m_enabled || d->volume <= 0.0f)
        return;
    auto it = d->cache.find(int(cue));
    if (it == d->cache.end())
        it = d->cache.insert(int(cue), synthesize(cue, kRate));
    if (!d->lane)
        d->lane = new QtAudioLane(d->deviceId, this);
    // Each cue is its own voice, so quick successive cues overlap instead of cutting off.
    d->lane->playSound(++d->serial, it.value(), kRate, d->volume);
}

QVector<float> Earcons::synthesize(Cue cue, int sampleRate)
{
    const int r = sampleRate > 0 ? sampleRate : kRate;
    switch (cue) {
    case Cue::ListenStart: // rising fifth
        return render(r, 170, {{659.25, 0, 75}, {987.77, 80, 90}});
    case Cue::ListenStop: // falling fifth
        return render(r, 170, {{987.77, 0, 75}, {659.25, 80, 90}});
    case Cue::Sent: // a soft tick
        return render(r, 70, {{1318.5, 0, 70, 0.7f, true}});
    case Cue::Error: // low double
        return render(r, 230, {{233.08, 0, 95, 1.2f}, {233.08, 125, 105, 1.2f}});
    case Cue::MicLive: // rising major triad: "attention, you're on air"
        return render(r, 240, {{523.25, 0, 70}, {659.25, 80, 70}, {783.99, 160, 80}});
    case Cue::MicMuted: // the same, falling
        return render(r, 240, {{783.99, 0, 70}, {659.25, 80, 70}, {523.25, 160, 80}});
    case Cue::Notify: // gentle two-tone chime
        return render(r, 250, {{880.0, 0, 250, 0.8f, true}, {1318.5, 60, 190, 0.5f, true}});
    }
    return {};
}
