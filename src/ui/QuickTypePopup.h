#pragma once

#include <QWidget>

class AppContext;
class QLabel;
class QLineEdit;

// A small always-on-top box summoned by a global shortcut: type, press Enter,
// it speaks and disappears — without leaving the game or app you're in.
class QuickTypePopup : public QWidget
{
    Q_OBJECT
public:
    explicit QuickTypePopup(AppContext *context, QWidget *parent = nullptr);

    void popup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    AppContext *m_ctx;
    QLineEdit *m_edit;
    QLabel *m_voice;
    int m_recallIndex = -1;
};
