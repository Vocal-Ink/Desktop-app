#include "obs/ObsIntegration.h"

#include "obs/ObsWebSocketClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

namespace {
// obs-websocket request status codes we react to.
constexpr int kResourceAlreadyExists = 601;
constexpr int kInvalidInputKind = 605;
constexpr int kDefaultTestClearMs = 4000;

#ifdef Q_OS_WIN
const char kTextInputKind[] = "text_gdiplus_v3";
const char kPreferredTextFamily[] = "text_gdiplus";
#else
const char kTextInputKind[] = "text_ft2_source_v2";
const char kPreferredTextFamily[] = "text_ft2_source";
#endif

using SceneCallback = std::function<void(const QString &scene, const QString &error)>;

bool isTextKind(const QString &unversionedKind)
{
    return unversionedKind == QLatin1String("text_gdiplus") || unversionedKind == QLatin1String("text_ft2_source");
}

QString unversionedKind(const QJsonObject &input)
{
    const QString kind = input.value(QStringLiteral("unversionedInputKind")).toString();
    if (!kind.isEmpty())
        return kind;
    static const QRegularExpression versionSuffix(QStringLiteral("_v\\d+$"));
    return input.value(QStringLiteral("inputKind")).toString().remove(versionSuffix);
}

QStringList sortedNames(QStringList names)
{
    names.removeAll(QString());
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

// Highest version of the platform's text source kind, else of any text kind.
QString pickTextKind(const QJsonArray &kinds)
{
    QStringList preferred;
    QStringList other;
    for (const QJsonValue &v : kinds) {
        const QString kind = v.toString();
        if (kind.startsWith(QLatin1String(kPreferredTextFamily)))
            preferred << kind;
        else if (kind.startsWith(QLatin1String("text_gdiplus")) || kind.startsWith(QLatin1String("text_ft2_source")))
            other << kind;
    }
    QStringList &list = preferred.isEmpty() ? other : preferred;
    list.sort();
    return list.isEmpty() ? QString() : list.constLast();
}

void requestProgramScene(ObsWebSocketClient *client, const SceneCallback &callback)
{
    client->request(QStringLiteral("GetCurrentProgramScene"), {},
                    [callback](bool ok, const QJsonObject &data, const QString &error) {
                        if (!ok) {
                            callback(QString(), error);
                            return;
                        }
                        // "sceneName" since obs-websocket 5.3, "currentProgramSceneName" before.
                        QString scene = data.value(QStringLiteral("sceneName")).toString();
                        if (scene.isEmpty())
                            scene = data.value(QStringLiteral("currentProgramSceneName")).toString();
                        callback(scene, scene.isEmpty() ? ObsIntegration::tr("OBS did not report its current scene.") : QString());
                    });
}

void createInput(ObsWebSocketClient *client, const QString &scene, const QString &name, const QString &kind,
                 const QJsonObject &settings, ObsWebSocketClient::Callback callback)
{
    const QJsonObject data{
        {QStringLiteral("sceneName"), scene},
        {QStringLiteral("inputName"), name},
        {QStringLiteral("inputKind"), kind},
        {QStringLiteral("inputSettings"), settings},
        {QStringLiteral("sceneItemEnabled"), true},
    };
    client->request(QStringLiteral("CreateInput"), data, std::move(callback));
}

int statusCode(const QJsonObject &requestStatus)
{
    return requestStatus.value(QStringLiteral("code")).toInt();
}
} // namespace

ObsIntegration::ObsIntegration(QObject *parent)
    : QObject(parent)
    , m_client(new ObsWebSocketClient(this))
    , m_clearTimer(new QTimer(this))
{
    m_statusText = tr("OBS integration is off");
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, [this] { setSubtitleText(QString()); });

    connect(m_client, &ObsWebSocketClient::identified, this, [this] {
        setStatus(Status::Connected, connectedText());
        if (m_subtitleShown) // left over from before the connection dropped
            setSubtitleText(QString());
    });
    connect(m_client, &ObsWebSocketClient::authenticationFailed, this, [this] {
        setStatus(Status::AuthFailed,
                  m_config.password.isEmpty()
                      ? tr("OBS asks for a WebSocket password. Copy it from Tools → WebSocket Server Settings → Show Connect Info.")
                      : tr("Wrong OBS WebSocket password"));
    });
    connect(m_client, &ObsWebSocketClient::errorOccurred, this, [this](const QString &message) {
        if (m_config.enabled)
            setStatus(Status::Error, message);
    });
    connect(m_client, &ObsWebSocketClient::stateChanged, this, [this](ObsWebSocketClient::State state) {
        if (state != ObsWebSocketClient::State::Disconnected)
            return;
        m_indicatorScene.clear();
        m_indicatorItemId = -1;
        if (m_config.enabled && m_status == Status::Connected)
            setStatus(Status::Connecting, tr("Lost the connection to OBS, reconnecting…"));
    });
}

ObsIntegration::~ObsIntegration()
{
    m_clearTimer->stop();
    if (m_subtitleShown)
        setSubtitleText(QString());
    m_client->disconnect(this);
    delete m_client; // sends what is still queued, then closes
    m_client = nullptr;
}

void ObsIntegration::setConfig(const Config &config)
{
    const Config old = m_config;
    m_config = config;

    if (m_subtitleShown && (!config.subtitles || config.subtitleSource != old.subtitleSource))
        sendSubtitle(old.subtitleSource, QString());
    if (m_indicatorItemId >= 0 && (!config.indicator || config.indicatorSource != old.indicatorSource))
        setIndicator(false);

    const bool connectionChanged = config.enabled != old.enabled || config.host != old.host
        || config.port != old.port || config.password != old.password;
    if (!connectionChanged)
        return;
    if (!config.enabled) {
        m_clearTimer->stop();
        m_client->disconnectFrom();
        setStatus(Status::Disabled, tr("OBS integration is off"));
        return;
    }
    startConnecting();
}

void ObsIntegration::reconnect()
{
    if (m_config.enabled)
        startConnecting();
}

void ObsIntegration::startConnecting()
{
    setStatus(Status::Connecting, tr("Connecting to OBS…"));
    m_client->connectTo(m_config.host, m_config.port, m_config.password);
}

void ObsIntegration::utteranceStarted(const QString &text)
{
    m_clearTimer->stop();
    if (!m_config.enabled || !m_client->isIdentified())
        return;
    if (m_config.subtitles)
        setSubtitleText(text);
    if (m_config.captions && !text.trimmed().isEmpty()) {
        // Fails harmlessly (e.g. "not streaming"), so the answer is ignored.
        m_client->request(QStringLiteral("SendStreamCaption"), {{QStringLiteral("captionText"), text}});
    }
    if (m_config.indicator)
        setIndicator(true);
}

void ObsIntegration::utteranceFinished(const QString &)
{
    setIndicator(false);
    if (m_config.subtitles && m_subtitleShown && m_config.clearAfterMs > 0)
        m_clearTimer->start(m_config.clearAfterMs);
}

void ObsIntegration::setSubtitleText(const QString &text)
{
    sendSubtitle(m_config.subtitleSource, text);
}

void ObsIntegration::sendSubtitle(const QString &source, const QString &text, DoneCallback done)
{
    if (source.isEmpty() || !m_client->isIdentified()) {
        if (done)
            finishLater(done, false, source.isEmpty() ? tr("Choose the text source for subtitles first.") : tr("Not connected to OBS"));
        return;
    }
    m_subtitleShown = !text.isEmpty();
    const QJsonObject data{
        {QStringLiteral("inputName"), source},
        {QStringLiteral("inputSettings"), QJsonObject{{QStringLiteral("text"), text}}},
        {QStringLiteral("overlay"), true},
    };
    m_client->request(QStringLiteral("SetInputSettings"), data,
                      [this, source, done](bool ok, const QJsonObject &, const QString &error) {
                          if (ok)
                              clearProblem();
                          else
                              reportProblem(tr("Could not update the text source “%1”: %2").arg(source, error));
                          if (done)
                              done(ok, error);
                      });
}

void ObsIntegration::setIndicator(bool visible)
{
    const quint64 request = ++m_indicatorRequest;
    if (!m_client->isIdentified())
        return;

    if (!visible) {
        if (m_indicatorItemId < 0)
            return;
        m_client->request(QStringLiteral("SetSceneItemEnabled"),
                          {{QStringLiteral("sceneName"), m_indicatorScene},
                           {QStringLiteral("sceneItemId"), m_indicatorItemId},
                           {QStringLiteral("sceneItemEnabled"), false}});
        m_indicatorScene.clear();
        m_indicatorItemId = -1;
        return;
    }

    const QString source = m_config.indicatorSource;
    if (source.isEmpty())
        return;
    requestProgramScene(m_client, [this, request, source](const QString &scene, const QString &) {
        if (request != m_indicatorRequest || scene.isEmpty())
            return;
        m_client->request(
            QStringLiteral("GetSceneItemId"),
            {{QStringLiteral("sceneName"), scene}, {QStringLiteral("sourceName"), source}},
            [this, request, scene, source](bool ok, const QJsonObject &data, const QString &) {
                if (request != m_indicatorRequest)
                    return;
                const int itemId = ok ? data.value(QStringLiteral("sceneItemId")).toInt(-1) : -1;
                if (itemId < 0) {
                    reportProblem(tr("The source “%1” is not in the scene “%2”.").arg(source, scene));
                    return;
                }
                m_indicatorScene = scene;
                m_indicatorItemId = itemId;
                m_client->request(QStringLiteral("SetSceneItemEnabled"),
                                  {{QStringLiteral("sceneName"), scene},
                                   {QStringLiteral("sceneItemId"), itemId},
                                   {QStringLiteral("sceneItemEnabled"), true}});
            });
    });
}

void ObsIntegration::listTextSources(ListCallback callback)
{
    m_client->request(QStringLiteral("GetInputList"), {},
                      [callback](bool ok, const QJsonObject &data, const QString &error) {
                          if (!ok) {
                              callback({}, error);
                              return;
                          }
                          QStringList names;
                          const QJsonArray inputs = data.value(QStringLiteral("inputs")).toArray();
                          for (const QJsonValue &v : inputs) {
                              const QJsonObject input = v.toObject();
                              if (isTextKind(unversionedKind(input)))
                                  names << input.value(QStringLiteral("inputName")).toString();
                          }
                          callback(sortedNames(names), QString());
                      });
}

void ObsIntegration::listAllSources(ListCallback callback)
{
    m_client->request(QStringLiteral("GetInputList"), {},
                      [callback](bool ok, const QJsonObject &data, const QString &error) {
                          if (!ok) {
                              callback({}, error);
                              return;
                          }
                          QStringList names;
                          const QJsonArray inputs = data.value(QStringLiteral("inputs")).toArray();
                          for (const QJsonValue &v : inputs)
                              names << v.toObject().value(QStringLiteral("inputName")).toString();
                          callback(sortedNames(names), QString());
                      });
}

void ObsIntegration::createTextSource(const QString &name, DoneCallback callback)
{
    const QString inputName = name.trimmed();
    if (inputName.isEmpty()) {
        finishLater(callback, false, tr("Enter a name for the new text source."));
        return;
    }
    const QJsonObject settings{
        {QStringLiteral("text"), QString()},
        {QStringLiteral("font"), QJsonObject{{QStringLiteral("face"), QStringLiteral("Arial")}, {QStringLiteral("size"), 48}}},
    };
    requestProgramScene(m_client, [this, inputName, settings, callback](const QString &scene, const QString &error) {
        if (scene.isEmpty()) {
            callback(false, error);
            return;
        }
        const QString created = tr("Added the text source “%1” to the scene “%2”.").arg(inputName, scene);
        createInput(m_client, scene, inputName, QLatin1String(kTextInputKind), settings,
                    [this, scene, inputName, settings, callback, created](bool ok, const QJsonObject &status, const QString &error) {
            if (ok || statusCode(status) != kInvalidInputKind) {
                callback(ok, ok ? created : error);
                return;
            }
            // Older or unusual OBS builds: use whichever text source version this one has.
            m_client->request(QStringLiteral("GetInputKindList"), {},
                              [this, scene, inputName, settings, callback, created](bool ok, const QJsonObject &data, const QString &error) {
                if (!ok) {
                    callback(false, error);
                    return;
                }
                const QString kind = pickTextKind(data.value(QStringLiteral("inputKinds")).toArray());
                if (kind.isEmpty()) {
                    callback(false, tr("This OBS installation has no Text source type."));
                    return;
                }
                createInput(m_client, scene, inputName, kind, settings,
                            [callback, created](bool ok, const QJsonObject &, const QString &error) {
                                callback(ok, ok ? created : error);
                            });
            });
        });
    });
}

void ObsIntegration::addBrowserOverlay(const QString &name, const QUrl &url, DoneCallback callback)
{
    if (!url.isValid() || url.isEmpty()) {
        finishLater(callback, false, tr("The caption overlay is not running. Turn it on first."));
        return;
    }
    const QString inputName = name.trimmed().isEmpty() ? tr("Vocal Ink captions") : name.trimmed();
    const QJsonObject settings{
        {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("width"), 1920},
        {QStringLiteral("height"), 1080},
        {QStringLiteral("css"), QString()},
    };
    requestProgramScene(m_client, [this, inputName, settings, callback](const QString &scene, const QString &error) {
        if (scene.isEmpty()) {
            callback(false, error);
            return;
        }
        createInput(m_client, scene, inputName, QStringLiteral("browser_source"), settings,
                    [this, scene, inputName, settings, callback](bool ok, const QJsonObject &status, const QString &error) {
            if (ok) {
                callback(true, tr("Added the browser source “%1” to the scene “%2”.").arg(inputName, scene));
                return;
            }
            if (statusCode(status) != kResourceAlreadyExists) {
                callback(false, error);
                return;
            }
            // Added before: point the existing browser source at the current URL.
            m_client->request(QStringLiteral("GetInputSettings"), {{QStringLiteral("inputName"), inputName}},
                              [this, inputName, settings, callback](bool ok, const QJsonObject &data, const QString &error) {
                if (!ok) {
                    callback(false, error);
                    return;
                }
                if (data.value(QStringLiteral("inputKind")).toString() != QLatin1String("browser_source")) {
                    callback(false, tr("OBS already has a source named “%1”. Choose another name.").arg(inputName));
                    return;
                }
                const QJsonObject update{
                    {QStringLiteral("inputName"), inputName},
                    {QStringLiteral("inputSettings"), settings},
                    {QStringLiteral("overlay"), true},
                };
                m_client->request(QStringLiteral("SetInputSettings"), update,
                                  [inputName, callback](bool ok, const QJsonObject &, const QString &error) {
                                      callback(ok, ok ? tr("Updated the browser source “%1”.").arg(inputName) : error);
                                  });
            });
        });
    });
}

void ObsIntegration::testSubtitle(DoneCallback callback)
{
    const QString source = m_config.subtitleSource;
    const QString text = tr("Vocal Ink test subtitle");
    m_clearTimer->stop();
    sendSubtitle(source, text, [this, source, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(false, error);
            return;
        }
        m_clearTimer->start(m_config.clearAfterMs > 0 ? m_config.clearAfterMs : kDefaultTestClearMs);
        callback(true, tr("Sent a test subtitle to “%1”.").arg(source));
    });
}

void ObsIntegration::finishLater(const DoneCallback &callback, bool ok, const QString &message)
{
    if (!callback)
        return;
    QTimer::singleShot(0, this, [callback, ok, message] { callback(ok, message); });
}

QString ObsIntegration::connectedText() const
{
    const QString version = m_client ? m_client->obsVersion() : QString();
    return version.isEmpty() ? tr("Connected to OBS") : tr("Connected to OBS %1").arg(version);
}

void ObsIntegration::reportProblem(const QString &problem)
{
    if (m_status == Status::Connected)
        setStatus(Status::Connected, tr("%1. %2").arg(connectedText(), problem));
}

void ObsIntegration::clearProblem()
{
    if (m_status == Status::Connected)
        setStatus(Status::Connected, connectedText());
}

void ObsIntegration::setStatus(Status status, const QString &text)
{
    if (m_status == status && m_statusText == text)
        return;
    m_status = status;
    m_statusText = text;
    emit statusChanged(status, text);
}
