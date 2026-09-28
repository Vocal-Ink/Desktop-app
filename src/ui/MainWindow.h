#pragma once

#include <QMainWindow>
#include <QPointer>

class AppContext;
class LevelMeter;
class MessageEdit;
class PhraseGrid;
class QComboBox;
class QLabel;
class QListView;
class QPushButton;
class QSlider;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class QuickTypePopup;
class SettingsDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppContext *context, QWidget *parent = nullptr);

    void setFirstRunWizardEnabled(bool enabled) { m_firstRunWizard = enabled; }
    void showSettings(const QString &page = QString());
    void runSetupWizard();
    void openVoicePicker();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void buildTray();
    void wire();
    void applyTheme();

    void speakMessage(const QString &text);
    void speakPhrase(int index);
    void refreshVoiceCombo();
    void updateMicButton();
    void updateStatus();
    void showBanner(const QString &message, int level, const QString &fixPage = QString());
    void historyMenu(const QPoint &pos);
    void showAndRaise();
    void fitPhrases();
    void refreshIcons();

    AppContext *m_ctx;
    bool m_firstRunWizard = true;
    bool m_shownOnce = false;
    bool m_quitting = false;
    bool m_trayHintShown = false;

    QComboBox *m_voiceCombo = nullptr;
    QSlider *m_speed = nullptr;
    QToolButton *m_sttChip = nullptr;
    QToolButton *m_obsChip = nullptr;
    QToolButton *m_overlayChip = nullptr;
    PhraseGrid *m_phrases = nullptr;
    class QScrollArea *m_phraseScroll = nullptr;
    MessageEdit *m_edit = nullptr;
    QPushButton *m_micButton = nullptr;
    LevelMeter *m_level = nullptr;
    QLabel *m_micStatus = nullptr;
    QPushButton *m_speakButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    QLabel *m_queueLabel = nullptr;
    QListView *m_history = nullptr;
    QLabel *m_banner = nullptr;
    QTimer *m_bannerTimer = nullptr;
    QLabel *m_outputLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QuickTypePopup *m_quickType = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
};
