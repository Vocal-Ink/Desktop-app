#include "avatar/AvatarController.h"

#include "avatar/StreamerbotSender.h"
#include "avatar/VeadotubeClient.h"
#include "avatar/VmcSender.h"
#include "avatar/VtsClient.h"
#include "core/Settings.h"

// Placeholder implementation of the contract; the real lip-sync lands with the
// VTubing backend.

AvatarController::AvatarController(Settings *settings, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_secrets(secrets)
    , m_vts(new VtsClient(secrets, this))
    , m_vmc(new VmcSender(this))
    , m_veado(new VeadotubeClient(this))
    , m_streamerbot(new StreamerbotSender(this))
{
}

AvatarController::~AvatarController() = default;

QObject *AvatarController::vtsObject() const
{
    return m_vts;
}

QObject *AvatarController::vmcObject() const
{
    return m_vmc;
}

QObject *AvatarController::veadoObject() const
{
    return m_veado;
}

void AvatarController::applySettings() {}
void AvatarController::setPluginIcon(const QByteArray &) {}
void AvatarController::onSpeechStarted(quint64, const QString &) {}
void AvatarController::onSpeechProgress(quint64, double) {}
void AvatarController::onSpeechFinished(quint64) {}
void AvatarController::onSpeechLevel(float) {}
void AvatarController::onOutputLevel(float) {}
void AvatarController::onMicLevel(float) {}
void AvatarController::onMicLiveChanged(bool) {}
void AvatarController::onSoundStarted(const QString &) {}
void AvatarController::onPanic() {}
void AvatarController::test(int) {}

AvatarController::Viseme AvatarController::visemeAt(const QString &, int)
{
    return Viseme::Rest;
}

QString AvatarController::visemeName(Viseme viseme)
{
    switch (viseme) {
    case Viseme::A: return QStringLiteral("A");
    case Viseme::I: return QStringLiteral("I");
    case Viseme::U: return QStringLiteral("U");
    case Viseme::E: return QStringLiteral("E");
    case Viseme::O: return QStringLiteral("O");
    case Viseme::Rest: break;
    }
    return QString();
}
