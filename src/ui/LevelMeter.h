#pragma once

#include <QWidget>

// Slim horizontal microphone level bar with peak smoothing.
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);

    void setLevel(float level); // 0..1
    void setActive(bool active); // highlight while speech is detected
    QSize sizeHint() const override { return {120, 8}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_level = 0.0f;
    bool m_active = false;
};
