#pragma once

#include <QDialog>
#include <functional>

class AppContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QWidget;

// All preferences. Changes apply immediately (no OK/Apply dance), which suits
// users who rely on a screen reader or switch access.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(AppContext *context, QWidget *parent = nullptr);

    // "general", "audio", "voices", "stt", "obs", "hotkeys", "text"
    void showPage(const QString &page);

signals:
    void themeChanged();
    void setupWizardRequested();

private:
    QWidget *buildGeneral();
    QWidget *buildAudio();
    QWidget *buildVoices();
    QWidget *buildStt();
    QWidget *buildObs();
    QWidget *buildHotkeys();
    QWidget *buildText();

    // Widget <-> setting helpers. `apply` runs after each change.
    QCheckBox *check(const QString &label, const char *key, std::function<void()> apply);
    QSlider *slider(const char *key, int min, int max, std::function<void()> apply);
    QWidget *sliderRow(const char *key, int min, int max, const QString &suffix, const QString &accessibleName,
                       std::function<void()> apply);
    QSpinBox *spin(const char *key, int min, int max, const QString &suffix, std::function<void()> apply);
    QLineEdit *line(const char *key, const QString &placeholder, std::function<void()> apply);
    QComboBox *combo(const char *key, const QList<QPair<QString, QVariant>> &items, std::function<void()> apply,
                     bool editable = false);
    QWidget *secret(const QString &name, const QString &placeholder, std::function<void()> apply = {});
    QComboBox *deviceCombo(const char *key, bool output);

    void refreshObsStatus();
    void refreshObsSources();
    void updateOverlayUrl();
    void saveReplacements();

    AppContext *m_ctx;
    QTabWidget *m_tabs;
    QLabel *m_obsStatus = nullptr;
    QComboBox *m_subtitleSource = nullptr;
    QComboBox *m_indicatorSource = nullptr;
    QLineEdit *m_overlayUrl = nullptr;
    QComboBox *m_overlayStyle = nullptr;
    QSpinBox *m_overlaySize = nullptr;
    QComboBox *m_overlayReveal = nullptr;
    QCheckBox *m_overlayName = nullptr;
    QTableWidget *m_replacements = nullptr;
};
