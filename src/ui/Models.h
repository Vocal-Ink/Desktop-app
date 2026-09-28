#pragma once

#include "tts/Voice.h"

#include <QAbstractListModel>
#include <QAudioDevice>
#include <QPointer>
#include <QStringList>

class ActionRegistry;
class ModelManager;
class PhraseStore;
class Settings;
class Soundboard;
class TtsRegistry;
class VoicePresets;

// Every voice from every provider, filtered for the voice browser.
class VoiceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY filterChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY filterChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY countChanged)
public:
    enum Roles {
        KeyRole = Qt::UserRole + 1, NameRole, ProviderRole, ProviderIdRole, LanguageRole, LanguageLabelRole,
        GenderRole, DescriptionRole, PreviewUrlRole, FavoriteRole, LocalRole, InitialsRole
    };

    VoiceModel(TtsRegistry *registry, Settings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString provider() const { return m_provider; }
    void setProvider(const QString &p);
    QString language() const { return m_language; }
    void setLanguage(const QString &l);
    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool f);
    QVariantList providers() const;   // [{id, name, available, reason, local, count}]
    QStringList languages() const;

    Q_INVOKABLE void toggleFavorite(const QString &key);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfKey(const QString &key) const;
    static QString languageLabel(const QString &code);

signals:
    void filterChanged();
    void countChanged();
    void providersChanged();

private:
    void rebuild();

    TtsRegistry *m_registry;
    Settings *m_settings;
    QList<Voice> m_all;
    QList<Voice> m_rows;
    QString m_search, m_provider, m_language;
    bool m_favoritesOnly = false;
};

class PhraseModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    enum Roles { TextRole = Qt::UserRole + 1, HotkeyRole, VoiceKeyRole, CategoryRole, ColorRole, IndexRole };

    explicit PhraseModel(PhraseStore *store, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QStringList categories() const;

    Q_INVOKABLE void add(const QString &text, const QString &category, const QString &color,
                         const QString &hotkey, const QString &voiceKey);
    Q_INVOKABLE void update(int index, const QString &text, const QString &category, const QString &color,
                            const QString &hotkey, const QString &voiceKey);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE void resetToDefaults();

signals:
    void changed();

private:
    PhraseStore *m_store;
};

class SoundModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, FileRole, HotkeyRole, GainRole, ColorRole, PlayingRole };

    explicit SoundModel(Soundboard *board, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString addFile(const QUrl &file);  // returns an error message or ""
    Q_INVOKABLE void update(const QString &id, const QString &name, const QString &color, const QString &hotkey,
                            double gain);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void play(const QString &id);
    Q_INVOKABLE void stopAll();

signals:
    void changed();

private:
    Soundboard *m_board;
};

class KeybindModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, CategoryRole, TitleRole, DescriptionRole, ShortcutRole, ShortcutTextRole,
        DefaultRole, IsDefaultRole, GlobalRole, HoldRole, WarningRole
    };

    explicit KeybindModel(ActionRegistry *registry, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the names of conflicting bindings (empty = none). Binds anyway
    // only when `force` is true.
    Q_INVOKABLE QStringList bind(const QString &id, const QString &sequence, bool force = false);
    Q_INVOKABLE void clear(const QString &id);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE QString shortcutFor(const QString &id) const;
    QStringList categories() const;

private:
    ActionRegistry *m_registry;
};

// Audio inputs or outputs, with virtual cables flagged.
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, VirtualRole, DefaultRole };

    DeviceModel(bool outputs, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int indexOfId(const QString &hexId) const;
    Q_INVOKABLE QString nameOf(const QString &hexId) const;
    Q_INVOKABLE void refresh();

signals:
    void countChanged();

private:
    bool m_outputs;
    QList<QAudioDevice> m_devices;
};

// Downloadable local models: Whisper (speech input) or Piper voices.
class DownloadModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool runtimeInstalled READ runtimeInstalled NOTIFY runtimeChanged)
    Q_PROPERTY(bool runtimeDownloading READ runtimeDownloading NOTIFY runtimeChanged)
    Q_PROPERTY(double runtimeProgress READ runtimeProgress NOTIFY runtimeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    enum class Kind { Whisper, Piper };
    enum Roles {
        TaskRole = Qt::UserRole + 1, NameRole, TitleRole, DetailsRole, SizeRole, RecommendedRole, InstalledRole,
        DownloadingRole, ProgressRole, InUseRole
    };

    DownloadModel(Kind kind, ModelManager *manager, Settings *settings, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    bool runtimeInstalled() const;
    bool runtimeDownloading() const;
    double runtimeProgress() const { return m_runtimeProgress; }
    QString status() const { return m_status; }

    Q_INVOKABLE void download(const QString &name);
    Q_INVOKABLE void cancel(const QString &name);
    Q_INVOKABLE void remove(const QString &name);
    Q_INVOKABLE void use(const QString &name); // Whisper: make it the active model
    Q_INVOKABLE void downloadRuntime();
    Q_INVOKABLE void downloadRecommended();
    Q_INVOKABLE QString recommended() const;
    void retranslate() { rebuild(); } // details are built in the current language
    // The interface language ("de", "pt_BR"...): picks the recommended model
    // or voice and sorts voices in that language first.
    void setLanguage(const QString &language);

signals:
    void filterChanged();
    void runtimeChanged();
    void statusChanged();

private:
    struct Row
    {
        QString name, title, details;
        qint64 size = 0;
        bool recommended = false;
    };
    void rebuild();
    QString taskId(const QString &name) const;
    int rowOfTask(const QString &task) const;
    void setStatus(const QString &s);
    QString locale() const;          // m_language with the system's region when it matches
    QString dictationLanguage() const;

    Kind m_kind;
    ModelManager *m_manager;
    Settings *m_settings;
    QList<Row> m_all;
    QList<Row> m_rows;
    QHash<QString, double> m_progress;
    double m_runtimeProgress = 0.0;
    QString m_filter;
    QString m_status;
    QString m_language;
};

class PresetModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, VoiceKeyRole, RateRole, PitchRole, EffectRole, IntensityRole };

    explicit PresetModel(VoicePresets *presets, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString add(const QString &name, const QString &voiceKey, int rate, int pitch, const QString &effect,
                            int intensity);
    Q_INVOKABLE void update(const QString &id, const QString &name, const QString &voiceKey, int rate, int pitch,
                            const QString &effect, int intensity);
    Q_INVOKABLE void remove(const QString &id);

signals:
    void changed();

private:
    VoicePresets *m_presets;
};
