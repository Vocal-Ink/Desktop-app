#include "audio/VoiceEffects.h"

#include "audio/AudioConvert.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace VoiceEffects {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxTailSeconds = 2.5;
constexpr double kTailFadeSeconds = 0.3;
constexpr float kSilence = 1e-3f; // -60 dBFS: a tail is over below this
constexpr double kMegaphoneLevel = 0.2; // RMS the megaphone's distortion is tuned for

inline float undenormal(float v)
{
    return std::fabs(v) < 1e-15f ? 0.0f : v;
}

// f moves from `from` (t = 0) to `to` (t = 1) on a log scale.
inline double logLerp(double from, double to, double t)
{
    return from * std::pow(to / from, t);
}

// RBJ "Audio EQ Cookbook" biquad, transposed direct form II.
struct Biquad
{
    enum Type { LowPass, HighPass, Peak };

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void set(Type type, double freq, double q, int rate, double gainDb = 0.0)
    {
        freq = std::clamp(freq, 10.0, 0.45 * rate);
        const double w0 = 2.0 * kPi * freq / rate;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        double n0 = 1.0, n1 = 0.0, n2 = 0.0, d0 = 1.0, d1 = 0.0, d2 = 0.0;
        switch (type) {
        case LowPass:
            n0 = (1.0 - cw) / 2.0;
            n1 = 1.0 - cw;
            n2 = n0;
            d0 = 1.0 + alpha;
            d1 = -2.0 * cw;
            d2 = 1.0 - alpha;
            break;
        case HighPass:
            n0 = (1.0 + cw) / 2.0;
            n1 = -(1.0 + cw);
            n2 = n0;
            d0 = 1.0 + alpha;
            d1 = -2.0 * cw;
            d2 = 1.0 - alpha;
            break;
        case Peak: {
            const double a = std::pow(10.0, gainDb / 40.0);
            n0 = 1.0 + alpha * a;
            n1 = -2.0 * cw;
            n2 = 1.0 - alpha * a;
            d0 = 1.0 + alpha / a;
            d1 = -2.0 * cw;
            d2 = 1.0 - alpha / a;
            break;
        }
        }
        b0 = n0 / d0;
        b1 = n1 / d0;
        b2 = n2 / d0;
        a1 = d1 / d0;
        a2 = d2 / d0;
    }

    float process(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (std::fabs(z1) < 1e-20)
            z1 = 0.0;
        if (std::fabs(z2) < 1e-20)
            z2 = 0.0;
        return float(y);
    }

    void reset() { z1 = z2 = 0.0; }
};

struct FilterBank
{
    std::vector<Biquad> stages;

    float process(float x)
    {
        for (Biquad &b : stages)
            x = b.process(x);
        return x;
    }
    void reset()
    {
        for (Biquad &b : stages)
            b.reset();
    }
};

// Circular delay line with fractional taps.
class DelayLine
{
public:
    void resize(qsizetype frames)
    {
        m_buf.assign(size_t(std::max<qsizetype>(frames, 2)), 0.0f);
        m_pos = 0;
    }
    // Sample written `delay` frames ago (1 = the previous one).
    float tap(double delay) const
    {
        const size_t n = m_buf.size();
        delay = std::clamp(delay, 1.0, double(n - 1));
        const size_t whole = size_t(delay);
        const float frac = float(delay - double(whole));
        const float a = m_buf[(m_pos + n - whole) % n];
        const float b = m_buf[(m_pos + n - whole - 1) % n];
        return a + (b - a) * frac;
    }
    void push(float v)
    {
        m_buf[m_pos] = undenormal(v);
        m_pos = (m_pos + 1) % m_buf.size();
    }
    void clear() { std::fill(m_buf.begin(), m_buf.end(), 0.0f); }
    float peak() const
    {
        float p = 0.0f;
        for (float v : m_buf)
            p = std::max(p, std::fabs(v));
        return p;
    }

private:
    std::vector<float> m_buf{0.0f, 0.0f};
    size_t m_pos = 0;
};

// Freeverb building blocks (Jezar at Dreampoint, public domain).
struct Comb
{
    std::vector<float> buf;
    size_t idx = 0;
    float store = 0.0f;
    float feedback = 0.9f;
    float damp = 0.25f;

    float process(float in)
    {
        const float out = buf[idx];
        store = undenormal(out * (1.0f - damp) + store * damp);
        buf[idx] = undenormal(in + store * feedback);
        idx = (idx + 1) % buf.size();
        return out;
    }
};

struct Allpass
{
    std::vector<float> buf;
    size_t idx = 0;

    float process(float in)
    {
        const float b = buf[idx];
        buf[idx] = undenormal(in + b * 0.5f);
        idx = (idx + 1) % buf.size();
        return b - in;
    }
};

constexpr std::array<int, 8> kCombTuning = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassTuning = {556, 441, 341, 225};

inline float cubicClip(float x)
{
    // Hard-ish: linear-ish in the middle, flat beyond +-1, no sharp corner.
    if (x >= 1.0f)
        return 2.0f / 3.0f;
    if (x <= -1.0f)
        return -2.0f / 3.0f;
    return x - x * x * x / 3.0f;
}

} // namespace

struct Chain::State
{
    FilterBank band;  // band-limiting before any distortion
    FilterBank post;  // smoothing after it
    DelayLine delay;  // echo, chorus or robot comb
    Biquad loop;      // echo feedback damping
    DelayLine preDelay;
    std::array<Comb, 8> combs;
    std::array<Allpass, 4> allpasses;
    double phase = 0.0;
    double phaseStep = 0.0;
    float makeup = 1.0f;
    float drive = 1.0f;
    double envelope = 0.0; // mean square, for level-independent distortion
    double envAttack = 0.0;
    double envRelease = 0.0;
    float crushLevels = 0.0f;
    double delayFrames = 0.0;
    double depthFrames = 0.0;
    float feedback = 0.0f;
    float wet = 0.0f;
    float dry = 1.0f;
    qsizetype tailQuietFrames = 0; // silence needed before a tail can be over
};

QList<Effect> all()
{
    return {Effect::None, Effect::Radio, Effect::Telephone, Effect::Robot, Effect::Echo,
            Effect::Cave, Effect::Underwater, Effect::Megaphone};
}

QString id(Effect effect)
{
    switch (effect) {
    case Effect::Radio: return QStringLiteral("radio");
    case Effect::Telephone: return QStringLiteral("telephone");
    case Effect::Robot: return QStringLiteral("robot");
    case Effect::Echo: return QStringLiteral("echo");
    case Effect::Cave: return QStringLiteral("cave");
    case Effect::Underwater: return QStringLiteral("underwater");
    case Effect::Megaphone: return QStringLiteral("megaphone");
    case Effect::None: break;
    }
    return QStringLiteral("none");
}

Effect fromId(const QString &value)
{
    for (Effect e : all()) {
        if (id(e) == value)
            return e;
    }
    return Effect::None;
}

QString displayName(Effect effect)
{
    switch (effect) {
    case Effect::Radio: return QCoreApplication::translate("VoiceEffects", "Radio");
    case Effect::Telephone: return QCoreApplication::translate("VoiceEffects", "Telephone");
    case Effect::Robot: return QCoreApplication::translate("VoiceEffects", "Robot");
    case Effect::Echo: return QCoreApplication::translate("VoiceEffects", "Echo");
    case Effect::Cave: return QCoreApplication::translate("VoiceEffects", "Cave");
    case Effect::Underwater: return QCoreApplication::translate("VoiceEffects", "Underwater");
    case Effect::Megaphone: return QCoreApplication::translate("VoiceEffects", "Megaphone");
    case Effect::None: break;
    }
    return QCoreApplication::translate("VoiceEffects", "No effect");
}

QString description(Effect effect)
{
    switch (effect) {
    case Effect::Radio:
        return QCoreApplication::translate("VoiceEffects", "Like a walkie-talkie or an old radio broadcast.");
    case Effect::Telephone:
        return QCoreApplication::translate("VoiceEffects", "Thin and tinny, like a voice on the phone.");
    case Effect::Robot:
        return QCoreApplication::translate("VoiceEffects", "A buzzing, metallic robot voice.");
    case Effect::Echo:
        return QCoreApplication::translate("VoiceEffects", "Your words bounce back a few times, like across a canyon.");
    case Effect::Cave:
        return QCoreApplication::translate("VoiceEffects", "A big, booming space with a long reverb tail.");
    case Effect::Underwater:
        return QCoreApplication::translate("VoiceEffects", "Muffled and wobbly, as if speaking from under water.");
    case Effect::Megaphone:
        return QCoreApplication::translate("VoiceEffects", "Loud, gritty and cutting, like a bullhorn.");
    case Effect::None:
        break;
    }
    return QCoreApplication::translate("VoiceEffects", "Your voice, unchanged.");
}

Chain::Chain() = default;
Chain::~Chain() = default;

void Chain::configure(Effect effect, float intensity, int sampleRate)
{
    if (sampleRate <= 0)
        sampleRate = 48000;
    intensity = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
    const bool rebuild = !m_state || effect != m_effect || sampleRate != m_rate;
    m_effect = effect;
    m_intensity = intensity;
    m_rate = sampleRate;
    if (rebuild)
        m_state = std::make_unique<State>();
    State &s = *m_state;

    const int rate = sampleRate;
    const double k = intensity;
    const double t = std::sqrt(k); // filters reach their character early
    const double top = std::min(20000.0, 0.45 * rate);
    // Coefficients are recomputed on every call; state survives intensity changes.
    const auto stages = [](FilterBank &b, size_t n) {
        if (b.stages.size() != n)
            b.stages.assign(n, Biquad());
    };

    switch (effect) {
    case Effect::Radio: {
        stages(s.band, 5);
        const double lo = logLerp(20.0, 500.0, t), hi = logLerp(top, 3200.0, t);
        s.band.stages[0].set(Biquad::HighPass, lo, 0.707, rate);
        s.band.stages[1].set(Biquad::HighPass, lo, 0.707, rate);
        s.band.stages[2].set(Biquad::LowPass, hi, 0.707, rate);
        s.band.stages[3].set(Biquad::LowPass, hi, 0.707, rate);
        s.band.stages[4].set(Biquad::Peak, 1400.0, 0.9, rate, 4.0 * t);
        s.drive = float(1.0 + 1.5 * k);
        s.makeup = float(1.0 + 0.4 * t + 0.4 * k);
        break;
    }
    case Effect::Telephone: {
        stages(s.band, 7);
        const double lo = logLerp(20.0, 300.0, t), hi = logLerp(top, 3400.0, t);
        for (int i = 0; i < 3; ++i) {
            s.band.stages[size_t(i)].set(Biquad::HighPass, lo, 0.707, rate);
            s.band.stages[size_t(i + 3)].set(Biquad::LowPass, hi, 0.707, rate);
        }
        s.band.stages[6].set(Biquad::Peak, 1800.0, 1.2, rate, 5.0 * t);
        s.makeup = float(1.0 + 0.3 * t);
        s.crushLevels = float(std::pow(2.0, 15.0 - 8.0 * k)); // 16 -> 8 bits
        break;
    }
    case Effect::Megaphone: {
        stages(s.band, 5);
        stages(s.post, 1);
        const double lo = logLerp(20.0, 600.0, t), hi = logLerp(top, 4000.0, t);
        s.band.stages[0].set(Biquad::HighPass, lo, 0.707, rate);
        s.band.stages[1].set(Biquad::HighPass, lo, 0.707, rate);
        s.band.stages[2].set(Biquad::LowPass, hi, 0.707, rate);
        s.band.stages[3].set(Biquad::LowPass, hi, 0.707, rate);
        s.band.stages[4].set(Biquad::Peak, 2000.0, 1.2, rate, 6.0 * t);
        s.post.stages[0].set(Biquad::LowPass, logLerp(top, 5000.0, t), 0.707, rate);
        s.drive = float(1.0 + 5.0 * k);
        s.makeup = float(1.0 / (1.5 * (1.0 + k))); // a little louder than the dry voice
        s.envAttack = 1.0 - std::exp(-1.0 / (0.01 * rate));
        s.envRelease = 1.0 - std::exp(-1.0 / (0.25 * rate));
        break;
    }
    case Effect::Underwater: {
        stages(s.band, 2);
        const double hi = logLerp(top, 600.0, t);
        s.band.stages[0].set(Biquad::LowPass, hi, 1.1, rate);
        s.band.stages[1].set(Biquad::LowPass, hi, 0.707, rate);
        if (rebuild)
            s.delay.resize(qsizetype(0.03 * rate));
        s.delayFrames = 0.012 * rate;
        s.depthFrames = 0.004 * rate * k;
        s.phaseStep = 2.0 * kPi * 0.7 / rate; // slow wobble
        s.wet = float(0.5 * k);
        s.makeup = float(1.0 + 0.5 * t);
        break;
    }
    case Effect::Robot:
        if (rebuild)
            s.delay.resize(qsizetype(0.02 * rate));
        s.phaseStep = 2.0 * kPi * 55.0 / rate; // ring-modulation carrier
        s.delayFrames = 0.0055 * rate;         // ~180 Hz metallic comb
        s.feedback = 0.45f;
        s.wet = float(k);
        s.makeup = 1.4f;
        s.tailQuietFrames = qsizetype(0.03 * rate);
        break;
    case Effect::Echo:
        if (rebuild)
            s.delay.resize(qsizetype(0.3 * rate));
        s.delayFrames = 0.28 * rate;
        s.feedback = 0.4f; // five or so audible repeats
        s.loop.set(Biquad::LowPass, 3500.0, 0.707, rate);
        s.wet = float(0.75 * k);
        s.tailQuietFrames = qsizetype(0.3 * rate);
        break;
    case Effect::Cave: {
        if (rebuild) {
            const double scale = double(rate) / 44100.0;
            for (size_t i = 0; i < s.combs.size(); ++i)
                s.combs[i].buf.assign(size_t(std::max(1.0, kCombTuning[i] * scale)), 0.0f);
            for (size_t i = 0; i < s.allpasses.size(); ++i)
                s.allpasses[i].buf.assign(size_t(std::max(1.0, kAllpassTuning[i] * scale)), 0.0f);
            s.preDelay.resize(qsizetype(0.035 * rate) + 1);
        }
        s.delayFrames = double(qsizetype(0.035 * rate));
        for (Comb &c : s.combs) {
            c.feedback = 0.9f;
            c.damp = 0.3f;
        }
        s.wet = float(k);
        s.dry = float(1.0 - 0.35 * k);
        s.tailQuietFrames = qsizetype(0.12 * rate);
        break;
    }
    case Effect::None:
        break;
    }
    if (s.tailQuietFrames == 0)
        s.tailQuietFrames = qsizetype(0.02 * rate);
}

void Chain::process(float *samples, qsizetype count)
{
    if (!isActive() || !m_state || !samples)
        return;
    State &s = *m_state;
    const float k = m_intensity;

    switch (m_effect) {
    case Effect::Radio:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = s.band.process(samples[i]);
            const float sat = std::tanh(x * s.drive) / s.drive; // rounds off the peaks only
            samples[i] = (x + k * (sat - x)) * s.makeup;
        }
        break;
    case Effect::Telephone:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = s.band.process(samples[i]) * s.makeup;
            samples[i] = std::round(x * s.crushLevels) / s.crushLevels;
        }
        break;
    case Effect::Megaphone:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = s.band.process(samples[i]);
            // Distort a level-normalised copy, then return to the voice's own level:
            // the same grit for quiet and loud voices.
            const double sq = double(x) * x;
            s.envelope += (sq > s.envelope ? s.envAttack : s.envRelease) * (sq - s.envelope);
            const float norm = float(std::clamp(kMegaphoneLevel / std::sqrt(s.envelope + 1e-12), 0.25, 4.0));
            const float wet = 1.5f * cubicClip(x * norm * s.drive) / norm * s.makeup;
            samples[i] = s.post.process(x + k * (wet - x));
        }
        break;
    case Effect::Underwater:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = s.band.process(samples[i]);
            s.delay.push(x);
            const double lfo = std::sin(s.phase);
            s.phase = std::fmod(s.phase + s.phaseStep, 2.0 * kPi);
            const float chorus = s.delay.tap(s.delayFrames + s.depthFrames * lfo);
            samples[i] = (x * (1.0f - 0.5f * s.wet) + chorus * s.wet) * s.makeup;
        }
        break;
    case Effect::Robot:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = samples[i];
            const float ring = x * float(std::sin(s.phase));
            s.phase = std::fmod(s.phase + s.phaseStep, 2.0 * kPi);
            const float comb = ring + s.feedback * s.delay.tap(s.delayFrames);
            s.delay.push(comb);
            samples[i] = x + s.wet * (comb * s.makeup - x);
        }
        break;
    case Effect::Echo:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = samples[i];
            const float e = s.delay.tap(s.delayFrames);
            s.delay.push(x + s.feedback * s.loop.process(e));
            samples[i] = x + s.wet * e;
        }
        break;
    case Effect::Cave:
        for (qsizetype i = 0; i < count; ++i) {
            const float x = samples[i];
            s.preDelay.push(x);
            const float in = s.preDelay.tap(s.delayFrames) * 0.015f;
            float acc = 0.0f;
            for (Comb &c : s.combs)
                acc += c.process(in);
            for (Allpass &a : s.allpasses)
                acc = a.process(acc);
            samples[i] = x * s.dry + acc * 2.2f * s.wet;
        }
        break;
    case Effect::None:
        return;
    }
    for (qsizetype i = 0; i < count; ++i)
        samples[i] = AudioConvert::softClip(samples[i]);
}

QVector<float> Chain::flushTail()
{
    if (!isActive() || !m_state)
        return {};
    const qsizetype maxFrames = qsizetype(kMaxTailSeconds * m_rate);
    const qsizetype block = std::max(1, m_rate / 50);
    const qsizetype quietNeeded = m_state->tailQuietFrames;
    QVector<float> out;
    qsizetype quiet = 0;
    while (out.size() < maxFrames && quiet < quietNeeded) {
        const qsizetype n = std::min(block, maxFrames - out.size());
        const qsizetype base = out.size();
        out.resize(base + n);
        std::fill(out.begin() + base, out.end(), 0.0f);
        process(out.data() + base, n);
        if (AudioConvert::peak(out.constData() + base, n) < kSilence)
            quiet += n;
        else
            quiet = 0;
    }
    const bool cut = quiet < quietNeeded;
    qsizetype end = out.size();
    while (end > 0 && std::fabs(out[end - 1]) < kSilence)
        --end;
    out.resize(end);
    if (cut) {
        // Still ringing at the limit: fade out rather than stop dead.
        const qsizetype fade = std::min(end, qsizetype(kTailFadeSeconds * m_rate));
        for (qsizetype i = 0; i < fade; ++i)
            out[end - fade + i] *= 1.0f - float(i + 1) / float(fade);
    }
    reset();
    return out;
}

void Chain::reset()
{
    if (!m_state)
        return;
    State &s = *m_state;
    s.band.reset();
    s.post.reset();
    s.delay.clear();
    s.loop.reset();
    s.preDelay.clear();
    for (Comb &c : s.combs) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.store = 0.0f;
        c.idx = 0;
    }
    for (Allpass &a : s.allpasses) {
        std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        a.idx = 0;
    }
    s.phase = 0.0;
    s.envelope = 0.0;
}

} // namespace VoiceEffects
