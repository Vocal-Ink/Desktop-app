#pragma once

#include <QByteArray>
#include <QObject>

// "Will Discord hear me?" — plays a short tone into the voice output and
// listens on the other end of the virtual cable to confirm it arrives.
class RoutingCheck : public QObject
{
    Q_OBJECT
public:
    explicit RoutingCheck(QObject *parent = nullptr);
    ~RoutingCheck() override;

    void start(const QByteArray &outputDevice, const QByteArray &inputDevice, int timeoutMs = 4000);
    void cancel();
    bool isRunning() const;

    // The capture device that carries what is played into `outputDeviceId`
    // (VB-CABLE: "CABLE Output" for "CABLE Input"; BlackHole: same name;
    // Vocal Ink virtual mic; Linux monitor/remap source). Empty if unknown.
    static QByteArray pairedInputFor(const QByteArray &outputDeviceId);

signals:
    void progress(float level);                     // 0..1 while listening
    void finished(bool heard, const QString &detail);

private:
    class Private;
    Private *d;
};
