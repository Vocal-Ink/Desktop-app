#include "stt/SttController.h"

// Placeholder implementation (replaced by the STT work package).
SttController::SttController(QObject *parent) : QObject(parent) {}
SttController::~SttController() = default;
void SttController::setEngine(SttEngine *engine) { m_engine = engine; }
void SttController::setMode(Mode mode) { m_mode = mode; }
void SttController::setInputDevice(const QByteArray &) {}
void SttController::setVadSensitivity(int) {}
void SttController::startListening() { emit errorOccurred(tr("Speech recognition is not available in this build.")); }
void SttController::stopListening() {}
void SttController::toggleListening() { startListening(); }
void SttController::cancel() {}
void SttController::onSamples(const QVector<float> &) {}
void SttController::submit(const QVector<float> &) {}
void SttController::setListening(bool listening) { m_listening = listening; }
SttController::Mode SttController::modeFromString(const QString &s)
{
    if (s == QLatin1String("toggle"))
        return Mode::Toggle;
    if (s == QLatin1String("vad"))
        return Mode::HandsFree;
    return Mode::PushToTalk;
}
QString SttController::modeToString(Mode m)
{
    switch (m) {
    case Mode::Toggle: return QStringLiteral("toggle");
    case Mode::HandsFree: return QStringLiteral("vad");
    case Mode::PushToTalk: break;
    }
    return QStringLiteral("ptt");
}
