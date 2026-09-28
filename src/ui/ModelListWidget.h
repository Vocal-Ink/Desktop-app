#pragma once

#include <QHash>
#include <QWidget>

class AppContext;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Downloadable local models with progress, used by Settings and the setup wizard.
//  Whisper:     speech recognition models; one is "in use"
//  PiperVoices: the Piper runtime plus the voice catalogue (searchable)
class ModelListWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Whisper, PiperVoices };

    ModelListWidget(Kind kind, AppContext *context, QWidget *parent = nullptr);

    // Only show recommended entries (for the setup wizard).
    void setRecommendedOnly(bool only);
    // Starts the download of the recommended entry (and the runtime for Piper).
    void downloadRecommended();
    bool hasInstalled() const;

signals:
    void installedChanged();

private:
    struct Row
    {
        QTreeWidgetItem *item = nullptr;
        QProgressBar *progress = nullptr;
        QPushButton *action = nullptr;
        QPushButton *use = nullptr;
    };

    void rebuild();
    void addRow(const QString &taskId, const QString &title, const QString &size, const QString &details,
                bool recommended);
    void refreshRow(const QString &taskId);
    void refreshRuntime();
    void onAction(const QString &taskId);
    bool isInstalled(const QString &taskId) const;
    void applyFilter();

    Kind m_kind;
    AppContext *m_ctx;
    bool m_recommendedOnly = false;
    QTreeWidget *m_tree;
    QLineEdit *m_filter = nullptr;
    QLabel *m_runtimeLabel = nullptr;
    QPushButton *m_runtimeButton = nullptr;
    QProgressBar *m_runtimeProgress = nullptr;
    QLabel *m_status;
    QHash<QString, Row> m_rows;
};
