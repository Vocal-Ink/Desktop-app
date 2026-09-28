#pragma once

#include "tts/Voice.h"

#include <QDialog>

class AppContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;
class VoicePreview;
class VoiceFilterModel;

// Browse, search, preview and favourite voices from every provider.
class VoicePickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VoicePickerDialog(AppContext *context, QWidget *parent = nullptr);

    Voice selectedVoice() const;

signals:
    void openSettingsRequested(const QString &page);

private:
    void reload();
    void appendVoices(const QList<Voice> &voices, bool fromSearch);
    void updateFilters();
    void updateButtons();
    void updateEngineNotes();
    void toggleFavorite();
    void addVoiceById();
    void remoteSearch();
    Voice voiceAt(const QModelIndex &proxyIndex) const;

    AppContext *m_ctx;
    VoicePreview *m_preview;
    QStandardItemModel *m_model;
    VoiceFilterModel *m_proxy;
    QTableView *m_table;
    QLineEdit *m_search;
    QComboBox *m_provider;
    QComboBox *m_language;
    QCheckBox *m_favoritesOnly;
    QLabel *m_notes;
    QLineEdit *m_previewText;
    QPushButton *m_previewButton;
    QPushButton *m_favButton;
    QPushButton *m_useButton;
    QPushButton *m_addById;
    bool m_previewPlaying = false;
};
