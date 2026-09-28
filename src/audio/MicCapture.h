#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

class QAudioSource;
class QIODevice;

// Captures the microphone and delivers 16 kHz mono float blocks (the format the
// recognisers want), converting from whatever the device supports.
class MicCapture : public QObject
{
    Q_OBJECT
public:
    static constexpr int SampleRate = 16000;

    explicit MicCapture(QObject *parent = nullptr);
    ~MicCapture() override;

    void setDevice(const QByteArray &deviceId); // empty = system default input
    bool start();                               // false + errorOccurred() on failure
    void stop();
    bool isRunning() const;
    QString deviceName() const;

signals:
    void samples(const QVector<float> &mono16k);
    void levelChanged(float level); // 0..1, roughly perceptual
    void errorOccurred(const QString &message);

private:
    class Private;
    Private *d;
};
