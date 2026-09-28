#include "platform/VirtualDriver.h"

#include "platform/VirtualDriverDetail.h"
#include "platform/VirtualDriverPlatform.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QTimer>
#include <QtConcurrent>

using VirtualDriverDetail::currentPlatform;
using VirtualDriverPlatform::Result;

namespace {

constexpr int PollIntervalMs = 500;

} // namespace

namespace VirtualDriverPlatform {

bool devicesVisible(const QString &outputName, const QString &inputName)
{
    const auto contains = [](const QList<QAudioDevice> &devices, const QString &name) {
        for (const QAudioDevice &dev : devices) {
            if (dev.description().contains(name, Qt::CaseInsensitive))
                return true;
        }
        return false;
    };
    return contains(QMediaDevices::audioOutputs(), outputName) && contains(QMediaDevices::audioInputs(), inputName);
}

#if !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
VirtualDriverDetail::Facts probe()
{
    return {};
}

Result install()
{
    Result r;
    r.message = VirtualDriverDetail::describe({}).text;
    return r;
}

Result uninstall()
{
    return install();
}
#endif

} // namespace VirtualDriverPlatform

VirtualDriver::VirtualDriver(QObject *parent)
    : QObject(parent)
    , m_mediaDevices(new QMediaDevices(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(PollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &VirtualDriver::pollDevices);
    // Devices come and go (driver installed elsewhere, coreaudiod restart, log-in).
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &VirtualDriver::refresh);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &VirtualDriver::refresh);
    refresh();
}

VirtualDriver::~VirtualDriver() = default;

bool VirtualDriver::needsAdmin() const
{
    const auto platform = currentPlatform();
    return platform == VirtualDriverDetail::Platform::MacOS || platform == VirtualDriverDetail::Platform::Windows;
}

QString VirtualDriver::displayName() const
{
    return QString::fromUtf8(VirtualDriverDetail::DisplayName);
}

QString VirtualDriver::outputDeviceName() const
{
    return VirtualDriverDetail::outputNameFor(currentPlatform());
}

QString VirtualDriver::inputDeviceName() const
{
    return VirtualDriverDetail::inputNameFor(currentPlatform());
}

QByteArray VirtualDriver::outputDeviceId() const
{
    const QString name = outputDeviceName();
    const auto outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &dev : outputs) {
        if (dev.description().contains(name, Qt::CaseInsensitive))
            return dev.id();
    }
    return {};
}

void VirtualDriver::refresh()
{
    // The running operation reports the final state itself.
    if (m_busy)
        return;
    VirtualDriverDetail::Facts facts = VirtualDriverPlatform::probe();
    if (facts.devicesVisible)
        m_restartPending = false;
    facts.restartPending = facts.restartPending || m_restartPending;
    const auto info = VirtualDriverDetail::describe(facts);
    setState(info.state, info.text);
}

void VirtualDriver::install()
{
    start(true);
}

void VirtualDriver::uninstall()
{
    start(false);
}

void VirtualDriver::start(bool install)
{
    if (m_busy) {
        emit finished(false, tr("Vocal Ink is still busy with the virtual mic. Try again in a moment."));
        return;
    }
    if (install && (m_state == State::Unsupported || m_state == State::Unavailable)) {
        emit finished(false, m_statusText);
        return;
    }
    setBusy(true);
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, install] {
        const Result result = watcher->result();
        watcher->deleteLater();
        handleResult(install, result);
    });
    watcher->setFuture(install ? QtConcurrent::run(&VirtualDriverPlatform::install)
                               : QtConcurrent::run(&VirtualDriverPlatform::uninstall));
}

void VirtualDriver::handleResult(bool install, const Result &result)
{
    switch (result.outcome) {
    case Result::Outcome::Cancelled:
        setBusy(false);
        refresh();
        emit finished(false, result.message);
        return;
    case Result::Outcome::Failed:
        setBusy(false);
        setState(State::Failed, result.message);
        emit finished(false, result.message);
        return;
    case Result::Outcome::OkRestartNeeded:
        m_restartPending = install;
        complete(true, result.message);
        return;
    case Result::Outcome::Ok:
        if (!install)
            m_restartPending = false;
        if (result.wait == Result::Wait::None) {
            complete(true, result.message);
            return;
        }
        m_waitForAppear = result.wait == Result::Wait::DevicesAppear;
        m_pollMessage = result.message;
        m_pollTimeoutMessage = result.timeoutMessage.isEmpty() ? result.message : result.timeoutMessage;
        m_restartOnTimeout = result.restartOnTimeout;
        m_pollAttemptsLeft = qMax(1, result.waitSeconds * 1000 / PollIntervalMs);
        pollDevices();
        if (m_busy)
            m_pollTimer->start();
        return;
    }
}

void VirtualDriver::pollDevices()
{
    const bool visible = VirtualDriverPlatform::probe().devicesVisible;
    if (visible == m_waitForAppear) {
        m_pollTimer->stop();
        complete(true, m_pollMessage);
        return;
    }
    if (--m_pollAttemptsLeft > 0)
        return;
    m_pollTimer->stop();
    if (m_restartOnTimeout)
        m_restartPending = true;
    complete(true, m_pollTimeoutMessage);
}

void VirtualDriver::complete(bool ok, const QString &message)
{
    setBusy(false);
    refresh();
    emit finished(ok, message);
}

void VirtualDriver::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit stateChanged();
}

void VirtualDriver::setState(State state, const QString &text)
{
    if (m_state == state && m_statusText == text)
        return;
    m_state = state;
    m_statusText = text;
    emit stateChanged();
}
