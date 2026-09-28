#include "ui/Models.h"

#include "audio/Soundboard.h"
#include "core/ActionRegistry.h"
#include "core/PhraseStore.h"
#include "core/Settings.h"
#include "core/VoicePresets.h"
#include "models/ModelManager.h"
#include "platform/VirtualAudio.h"
#include "tts/TtsRegistry.h"

#include <QKeySequence>
#include <QLocale>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace {
QString sizeText(qint64 bytes)
{
    return bytes > 0 ? QLocale().formattedDataSize(bytes, 0) : QString();
}

QString initialsOf(const QString &name)
{
    QString out;
    const QStringList words = name.split(QRegularExpression(QStringLiteral("[\\s_\\-()]+")), Qt::SkipEmptyParts);
    for (const QString &w : words) {
        if (!w.isEmpty() && w.at(0).isLetterOrNumber())
            out += w.at(0).toUpper();
        if (out.size() == 2)
            break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}
} // namespace

// --- VoiceModel -----------------------------------------------------------------

VoiceModel::VoiceModel(TtsRegistry *registry, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_registry(registry)
    , m_settings(settings)
{
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(120);
    connect(debounce, &QTimer::timeout, this, [this] {
        m_all = m_registry->allVoices();
        rebuild();
        emit providersChanged();
    });
    connect(m_registry, &TtsRegistry::voicesChanged, debounce, qOverload<>(&QTimer::start));
    connect(m_settings, &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(Keys::FavoriteVoices)) {
            if (m_favoritesOnly)
                rebuild();
            else if (!m_rows.isEmpty())
                emit dataChanged(index(0), index(int(m_rows.size()) - 1), {FavoriteRole});
        }
    });
    m_all = m_registry->allVoices();
    rebuild();
}

QString VoiceModel::languageLabel(const QString &code)
{
    if (code.isEmpty())
        return tr("Any language");
    const QLocale locale(QString(code).replace(QLatin1Char('-'), QLatin1Char('_')));
    if (locale.language() == QLocale::C)
        return code;
    const QString name = QLocale::languageToString(locale.language());
    if (code.contains(QLatin1Char('-')) || code.contains(QLatin1Char('_')))
        return QStringLiteral("%1 (%2)").arg(name, QLocale::territoryToString(locale.territory()));
    return name;
}

void VoiceModel::rebuild()
{
    const QStringList favorites = m_settings->value(Keys::FavoriteVoices).toStringList();
    const QStringList words = m_search.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    beginResetModel();
    m_rows.clear();
    for (const Voice &v : std::as_const(m_all)) {
        if (!m_provider.isEmpty() && v.engineId != m_provider)
            continue;
        if (m_favoritesOnly && !favorites.contains(v.key()))
            continue;
        if (!m_language.isEmpty() && languageLabel(v.language) != m_language)
            continue;
        if (!words.isEmpty()) {
            const TtsEngine *e = m_registry->engine(v.engineId);
            const QString hay = v.name + QLatin1Char(' ') + v.description + QLatin1Char(' ') + v.gender
                + QLatin1Char(' ') + languageLabel(v.language) + QLatin1Char(' ') + (e ? e->displayName() : QString());
            bool all = true;
            for (const QString &w : words)
                all = all && hay.contains(w, Qt::CaseInsensitive);
            if (!all)
                continue;
        }
        m_rows << v;
    }
    // Favorites first, then local voices, then by name.
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](const Voice &a, const Voice &b) {
        const bool fa = favorites.contains(a.key()), fb = favorites.contains(b.key());
        if (fa != fb)
            return fa;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    endResetModel();
    emit countChanged();
}

int VoiceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant VoiceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Voice &v = m_rows.at(index.row());
    const TtsEngine *e = m_registry->engine(v.engineId);
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return v.name;
    case KeyRole: return v.key();
    case ProviderRole: return e ? e->displayName() : v.engineId;
    case ProviderIdRole: return v.engineId;
    case LanguageRole: return v.language;
    case LanguageLabelRole: return languageLabel(v.language);
    case GenderRole: return v.gender;
    case DescriptionRole: return v.description;
    case PreviewUrlRole: return v.previewUrl;
    case FavoriteRole: return m_settings->value(Keys::FavoriteVoices).toStringList().contains(v.key());
    case LocalRole: return e && e->isLocal();
    case InitialsRole: return initialsOf(v.name);
    default: return {};
    }
}

QHash<int, QByteArray> VoiceModel::roleNames() const
{
    return {{KeyRole, "key"},
            {NameRole, "name"},
            {ProviderRole, "provider"},
            {ProviderIdRole, "providerId"},
            {LanguageRole, "language"},
            {LanguageLabelRole, "languageLabel"},
            {GenderRole, "gender"},
            {DescriptionRole, "description"},
            {PreviewUrlRole, "previewUrl"},
            {FavoriteRole, "favorite"},
            {LocalRole, "local"},
            {InitialsRole, "initials"}};
}

void VoiceModel::setSearch(const QString &s)
{
    if (s == m_search)
        return;
    m_search = s;
    rebuild();
    emit filterChanged();
}

void VoiceModel::setProvider(const QString &p)
{
    if (p == m_provider)
        return;
    m_provider = p;
    rebuild();
    emit filterChanged();
}

void VoiceModel::setLanguage(const QString &l)
{
    if (l == m_language)
        return;
    m_language = l;
    rebuild();
    emit filterChanged();
}

void VoiceModel::setFavoritesOnly(bool f)
{
    if (f == m_favoritesOnly)
        return;
    m_favoritesOnly = f;
    rebuild();
    emit filterChanged();
}

QVariantList VoiceModel::providers() const
{
    QVariantList list;
    for (TtsEngine *e : m_registry->engines()) {
        int count = 0;
        for (const Voice &v : m_all)
            count += v.engineId == e->id() ? 1 : 0;
        list << QVariantMap{{QStringLiteral("id"), e->id()},
                            {QStringLiteral("name"), e->displayName()},
                            {QStringLiteral("available"), e->isAvailable()},
                            {QStringLiteral("reason"), e->unavailableReason()},
                            {QStringLiteral("local"), e->isLocal()},
                            {QStringLiteral("count"), count},
                            {QStringLiteral("searchable"), e->supportsRemoteSearch()},
                            {QStringLiteral("customIds"), e->supportsCustomVoiceIds()}};
    }
    return list;
}

QStringList VoiceModel::languages() const
{
    QSet<QString> set;
    for (const Voice &v : m_all)
        set.insert(languageLabel(v.language));
    QStringList list(set.begin(), set.end());
    list.sort(Qt::CaseInsensitive);
    return list;
}

void VoiceModel::toggleFavorite(const QString &key)
{
    QStringList favorites = m_settings->value(Keys::FavoriteVoices).toStringList();
    if (favorites.contains(key))
        favorites.removeAll(key);
    else
        favorites << key;
    m_settings->setValue(Keys::FavoriteVoices, favorites);
}

QVariantMap VoiceModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

int VoiceModel::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key() == key)
            return i;
    }
    return -1;
}

// --- PhraseModel ------------------------------------------------------------------

PhraseModel::PhraseModel(PhraseStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    connect(m_store, &PhraseStore::changed, this, [this] {
        beginResetModel();
        endResetModel();
        emit changed();
    });
}

int PhraseModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_store->phrases().size());
}

QVariant PhraseModel::data(const QModelIndex &index, int role) const
{
    const QList<Phrase> &list = m_store->phrases();
    if (!index.isValid() || index.row() >= list.size())
        return {};
    const Phrase &p = list.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return p.text;
    case HotkeyRole: return p.hotkey;
    case VoiceKeyRole: return p.voiceKey;
    case CategoryRole: return p.category.isEmpty() ? tr("Basics") : p.category;
    case ColorRole: return p.color;
    case IndexRole: return index.row();
    default: return {};
    }
}

QHash<int, QByteArray> PhraseModel::roleNames() const
{
    return {{TextRole, "text"},         {HotkeyRole, "hotkey"}, {VoiceKeyRole, "voiceKey"},
            {CategoryRole, "category"}, {ColorRole, "tint"},    {IndexRole, "phraseIndex"}};
}

QStringList PhraseModel::categories() const
{
    return m_store->categories();
}

void PhraseModel::add(const QString &text, const QString &category, const QString &color, const QString &hotkey,
                      const QString &voiceKey)
{
    if (text.trimmed().isEmpty())
        return;
    m_store->add({text.trimmed(), hotkey, voiceKey, category.trimmed(), color});
}

void PhraseModel::update(int index, const QString &text, const QString &category, const QString &color,
                         const QString &hotkey, const QString &voiceKey)
{
    if (text.trimmed().isEmpty())
        return;
    m_store->update(index, {text.trimmed(), hotkey, voiceKey, category.trimmed(), color});
}

void PhraseModel::remove(int index)
{
    m_store->removeAt(index);
}

void PhraseModel::move(int from, int to)
{
    m_store->move(from, to);
}

QVariantMap PhraseModel::get(int i) const
{
    QVariantMap map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(i), it.key()));
    return map;
}

void PhraseModel::resetToDefaults()
{
    m_store->setPhrases(PhraseStore::defaultPhrases());
}

// --- SoundModel ---------------------------------------------------------------------

SoundModel::SoundModel(Soundboard *board, QObject *parent)
    : QAbstractListModel(parent)
    , m_board(board)
{
    connect(m_board, &Soundboard::changed, this, [this] {
        beginResetModel();
        endResetModel();
        emit changed();
    });
    connect(m_board, &Soundboard::playingChanged, this, [this](const QString &id, bool) {
        const int row = m_board->indexOf(id);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {PlayingRole});
    });
}

int SoundModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_board->sounds().size());
}

QVariant SoundModel::data(const QModelIndex &index, int role) const
{
    const QList<Sound> &list = m_board->sounds();
    if (!index.isValid() || index.row() >= list.size())
        return {};
    const Sound &s = list.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return s.name;
    case IdRole: return s.id;
    case FileRole: return s.file;
    case HotkeyRole: return s.hotkey;
    case GainRole: return s.gain;
    case ColorRole: return s.color;
    case PlayingRole: return m_board->isPlaying(s.id);
    default: return {};
    }
}

QHash<int, QByteArray> SoundModel::roleNames() const
{
    return {{IdRole, "soundId"}, {NameRole, "name"}, {FileRole, "file"},      {HotkeyRole, "hotkey"},
            {GainRole, "gain"},  {ColorRole, "tint"}, {PlayingRole, "playing"}};
}

QString SoundModel::addFile(const QUrl &file)
{
    QString error;
    const QString id = m_board->addFile(file.isLocalFile() ? file.toLocalFile() : file.toString(), &error);
    return id.isEmpty() ? (error.isEmpty() ? tr("Couldn't add that sound.") : error) : QString();
}

void SoundModel::update(const QString &id, const QString &name, const QString &color, const QString &hotkey, double gain)
{
    const int row = m_board->indexOf(id);
    if (row < 0)
        return;
    Sound s = m_board->sounds().at(row);
    s.name = name.trimmed().isEmpty() ? s.name : name.trimmed();
    s.color = color;
    s.hotkey = hotkey;
    s.gain = float(gain);
    m_board->update(s);
}

void SoundModel::remove(const QString &id)
{
    m_board->remove(id);
}

void SoundModel::play(const QString &id)
{
    m_board->play(id);
}

void SoundModel::stopAll()
{
    m_board->stopAll();
}

// --- KeybindModel -------------------------------------------------------------------

KeybindModel::KeybindModel(ActionRegistry *registry, QObject *parent)
    : QAbstractListModel(parent)
    , m_registry(registry)
{
    connect(m_registry, &ActionRegistry::shortcutChanged, this, [this](const QString &id) {
        const QList<ActionDef> actions = m_registry->actions();
        for (int i = 0; i < actions.size(); ++i) {
            if (actions.at(i).id == id)
                emit dataChanged(index(i), index(i));
        }
    });
    connect(m_registry, &ActionRegistry::shortcutsReset, this, [this] {
        beginResetModel();
        endResetModel();
    });
    connect(m_registry, &ActionRegistry::actionsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

int KeybindModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_registry->actions().size());
}

QVariant KeybindModel::data(const QModelIndex &index, int role) const
{
    const QList<ActionDef> actions = m_registry->actions();
    if (!index.isValid() || index.row() >= actions.size())
        return {};
    const ActionDef &a = actions.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return a.title;
    case IdRole: return a.id;
    case CategoryRole: return a.category;
    case DescriptionRole: return a.description;
    case ShortcutRole: return m_registry->shortcut(a.id);
    case ShortcutTextRole:
        return QKeySequence::fromString(m_registry->shortcut(a.id), QKeySequence::PortableText)
            .toString(QKeySequence::NativeText);
    case DefaultRole: return a.defaultShortcut;
    case IsDefaultRole: return m_registry->isDefault(a.id);
    case GlobalRole: return a.global;
    case HoldRole: return a.hold;
    case WarningRole: return a.warning;
    default: return {};
    }
}

QHash<int, QByteArray> KeybindModel::roleNames() const
{
    return {{IdRole, "actionId"},        {CategoryRole, "category"},   {TitleRole, "title"},
            {DescriptionRole, "description"}, {ShortcutRole, "shortcut"}, {ShortcutTextRole, "shortcutText"},
            {DefaultRole, "defaultShortcut"}, {IsDefaultRole, "isDefault"}, {GlobalRole, "isGlobal"},
            {HoldRole, "hold"},          {WarningRole, "warning"}};
}

QStringList KeybindModel::bind(const QString &id, const QString &sequence, bool force)
{
    const QStringList conflicts = m_registry->conflictsWith(id, sequence);
    if (!conflicts.isEmpty() && !force)
        return conflicts;
    m_registry->setShortcut(id, sequence);
    return {};
}

void KeybindModel::clear(const QString &id)
{
    m_registry->setShortcut(id, QString());
}

void KeybindModel::reset(const QString &id)
{
    m_registry->resetToDefault(id);
}

void KeybindModel::resetAll()
{
    m_registry->resetAll();
}

QStringList KeybindModel::categories() const
{
    return m_registry->categories();
}

QString KeybindModel::shortcutFor(const QString &id) const
{
    return QKeySequence::fromString(m_registry->shortcut(id), QKeySequence::PortableText)
        .toString(QKeySequence::NativeText);
}

// --- DeviceModel ----------------------------------------------------------------------

DeviceModel::DeviceModel(bool outputs, QObject *parent)
    : QAbstractListModel(parent)
    , m_outputs(outputs)
{
    auto *devices = new QMediaDevices(this);
    connect(devices, outputs ? &QMediaDevices::audioOutputsChanged : &QMediaDevices::audioInputsChanged, this,
            &DeviceModel::refresh);
    refresh();
}

void DeviceModel::refresh()
{
    beginResetModel();
    m_devices = m_outputs ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    endResetModel();
    emit countChanged();
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    // Row 0 is "System default".
    return parent.isValid() ? 0 : int(m_devices.size()) + 1;
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() > m_devices.size())
        return {};
    if (index.row() == 0) {
        switch (role) {
        case Qt::DisplayRole:
        case NameRole: return tr("System default");
        case IdRole: return QString();
        case VirtualRole: return false;
        case DefaultRole: return true;
        default: return {};
        }
    }
    const QAudioDevice &d = m_devices.at(index.row() - 1);
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return d.description();
    case IdRole: return QString::fromLatin1(d.id().toHex());
    case VirtualRole: return VirtualAudio::looksLikeVirtualCable(d.description());
    case DefaultRole: return d.isDefault();
    default: return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {{IdRole, "deviceId"}, {NameRole, "name"}, {VirtualRole, "isVirtual"}, {DefaultRole, "isDefault"}};
}

int DeviceModel::indexOfId(const QString &hexId) const
{
    if (hexId.isEmpty())
        return 0;
    const QByteArray id = QByteArray::fromHex(hexId.toLatin1());
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).id() == id)
            return i + 1;
    }
    return -1;
}

QString DeviceModel::nameOf(const QString &hexId) const
{
    const int row = indexOfId(hexId);
    return row < 0 ? tr("Disconnected device") : data(index(row), NameRole).toString();
}

// --- DownloadModel ---------------------------------------------------------------------

DownloadModel::DownloadModel(Kind kind, ModelManager *manager, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_kind(kind)
    , m_manager(manager)
    , m_settings(settings)
{
    connect(m_manager, &ModelManager::downloadProgress, this, [this](const QString &task, qint64 got, qint64 total) {
        const double p = total > 0 ? double(got) / double(total) : 0.0;
        if (task == QLatin1String("piper-runtime")) {
            m_runtimeProgress = p;
            emit runtimeChanged();
            return;
        }
        m_progress.insert(task, p);
        const int row = rowOfTask(task);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {ProgressRole, DownloadingRole});
    });
    auto refreshTask = [this](const QString &task) {
        m_progress.remove(task);
        if (task == QLatin1String("piper-runtime")) {
            emit runtimeChanged();
            return;
        }
        const int row = rowOfTask(task);
        if (row >= 0)
            emit dataChanged(index(row), index(row));
    };
    connect(m_manager, &ModelManager::downloadFinished, this, [this, refreshTask](const QString &task) {
        refreshTask(task);
        setStatus(tr("Download complete."));
    });
    connect(m_manager, &ModelManager::downloadFailed, this, [this, refreshTask](const QString &task, const QString &error) {
        refreshTask(task);
        setStatus(error);
    });
    connect(m_manager, &ModelManager::installedChanged, this, [this] {
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
        emit runtimeChanged();
    });
    if (kind == Kind::Piper) {
        connect(m_manager, &ModelManager::piperCatalogChanged, this, &DownloadModel::rebuild);
        connect(m_manager, &ModelManager::piperCatalogError, this, [this](const QString &msg) { setStatus(msg); });
        m_manager->refreshPiperCatalog();
    } else {
        connect(m_settings, &Settings::changed, this, [this](const QString &key) {
            if (key == QLatin1String(Keys::WhisperModel) && !m_rows.isEmpty())
                emit dataChanged(index(0), index(int(m_rows.size()) - 1), {InUseRole});
        });
    }
    rebuild();
}

void DownloadModel::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged();
}

QString DownloadModel::taskId(const QString &name) const
{
    return (m_kind == Kind::Whisper ? QStringLiteral("whisper:") : QStringLiteral("piper-voice:")) + name;
}

int DownloadModel::rowOfTask(const QString &task) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (taskId(m_rows.at(i).name) == task)
            return i;
    }
    return -1;
}

void DownloadModel::rebuild()
{
    m_all.clear();
    if (m_kind == Kind::Whisper) {
        const QList<WhisperModelInfo> catalog = ModelManager::whisperCatalog();
        for (const WhisperModelInfo &m : catalog)
            m_all << Row{m.file, m.title, m.description, m.approxBytes, m.recommended};
    } else {
        QList<PiperVoiceInfo> voices = m_manager->piperCatalog();
        QString myLang = QLocale().name();
        if (myLang == QLatin1String("C"))
            myLang = QStringLiteral("en_US");
        const QString myShort = myLang.section(QLatin1Char('_'), 0, 0);
        std::stable_sort(voices.begin(), voices.end(), [&](const PiperVoiceInfo &a, const PiperVoiceInfo &b) {
            auto rank = [&](const PiperVoiceInfo &v) {
                return v.languageCode == myLang ? 0 : v.languageCode.startsWith(myShort) ? 1 : 2;
            };
            return rank(a) != rank(b) ? rank(a) < rank(b) : a.key < b.key;
        });
        const QString rec = ModelManager::recommendedPiperVoice();
        for (const PiperVoiceInfo &v : std::as_const(voices)) {
            QString details = QStringLiteral("%1 · %2").arg(v.languageName, v.quality);
            if (v.numSpeakers > 1)
                details += QStringLiteral(" · ") + tr("%n speaker(s)", nullptr, v.numSpeakers);
            m_all << Row{v.key, v.name, details, v.totalBytes(), v.key == rec};
        }
    }
    setFilter(m_filter); // applies the filter and resets the model
    beginResetModel();
    m_rows.clear();
    for (const Row &r : std::as_const(m_all)) {
        if (m_filter.isEmpty() || r.name.contains(m_filter, Qt::CaseInsensitive)
            || r.title.contains(m_filter, Qt::CaseInsensitive) || r.details.contains(m_filter, Qt::CaseInsensitive))
            m_rows << r;
    }
    endResetModel();
}

void DownloadModel::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    rebuild();
    emit filterChanged();
}

int DownloadModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant DownloadModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const QString task = taskId(r.name);
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case TaskRole: return task;
    case NameRole: return r.name;
    case DetailsRole: return r.details;
    case SizeRole: return sizeText(r.size);
    case RecommendedRole: return r.recommended;
    case InstalledRole:
        return m_kind == Kind::Whisper ? m_manager->isWhisperModelInstalled(r.name)
                                       : m_manager->isPiperVoiceInstalled(r.name);
    case DownloadingRole: return m_manager->isDownloading(task);
    case ProgressRole: return m_progress.value(task, 0.0);
    case InUseRole:
        return m_kind == Kind::Whisper && m_settings->string(Keys::WhisperModel) == r.name;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadModel::roleNames() const
{
    return {{TaskRole, "task"},           {NameRole, "name"},           {TitleRole, "title"},
            {DetailsRole, "details"},     {SizeRole, "size"},           {RecommendedRole, "recommended"},
            {InstalledRole, "installed"}, {DownloadingRole, "downloading"}, {ProgressRole, "progress"},
            {InUseRole, "inUse"}};
}

bool DownloadModel::runtimeInstalled() const
{
    return m_kind == Kind::Whisper || m_manager->isPiperRuntimeInstalled();
}

bool DownloadModel::runtimeDownloading() const
{
    return m_kind == Kind::Piper && m_manager->isDownloading(QStringLiteral("piper-runtime"));
}

void DownloadModel::download(const QString &name)
{
    if (m_kind == Kind::Whisper) {
        m_manager->downloadWhisperModel(name);
        if (!m_manager->isWhisperModelInstalled(m_settings->string(Keys::WhisperModel)))
            m_settings->setValue(Keys::WhisperModel, name);
    } else {
        if (!m_manager->isPiperRuntimeInstalled() && !runtimeDownloading())
            m_manager->downloadPiperRuntime();
        m_manager->downloadPiperVoice(name);
    }
    const int row = rowOfTask(taskId(name));
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit runtimeChanged();
}

void DownloadModel::cancel(const QString &name)
{
    m_manager->cancel(taskId(name));
}

void DownloadModel::remove(const QString &name)
{
    if (m_kind == Kind::Whisper)
        m_manager->removeWhisperModel(name);
    else
        m_manager->removePiperVoice(name);
}

void DownloadModel::use(const QString &name)
{
    if (m_kind == Kind::Whisper)
        m_settings->setValue(Keys::WhisperModel, name);
}

void DownloadModel::downloadRuntime()
{
    if (m_kind == Kind::Piper && !m_manager->isPiperRuntimeInstalled())
        m_manager->downloadPiperRuntime();
    emit runtimeChanged();
}

QString DownloadModel::recommended() const
{
    for (const Row &r : m_all) {
        if (r.recommended)
            return r.name;
    }
    return m_kind == Kind::Piper ? ModelManager::recommendedPiperVoice() : QStringLiteral("ggml-base.en-q5_1.bin");
}

void DownloadModel::downloadRecommended()
{
    const QString name = recommended();
    const bool installed = m_kind == Kind::Whisper ? m_manager->isWhisperModelInstalled(name)
                                                   : m_manager->isPiperVoiceInstalled(name);
    if (!installed && !m_manager->isDownloading(taskId(name)))
        download(name);
    if (m_kind == Kind::Piper)
        downloadRuntime();
}

// --- PresetModel ----------------------------------------------------------------------

PresetModel::PresetModel(VoicePresets *presets, QObject *parent)
    : QAbstractListModel(parent)
    , m_presets(presets)
{
    connect(m_presets, &VoicePresets::changed, this, [this] {
        beginResetModel();
        endResetModel();
        emit changed();
    });
}

int PresetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_presets->presets().size());
}

QVariant PresetModel::data(const QModelIndex &index, int role) const
{
    const QList<VoicePreset> &list = m_presets->presets();
    if (!index.isValid() || index.row() >= list.size())
        return {};
    const VoicePreset &p = list.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return p.name;
    case IdRole: return p.id;
    case VoiceKeyRole: return p.voiceKey;
    case RateRole: return p.rate;
    case PitchRole: return p.pitch;
    case EffectRole: return p.effect;
    case IntensityRole: return p.effectIntensity;
    default: return {};
    }
}

QHash<int, QByteArray> PresetModel::roleNames() const
{
    return {{IdRole, "presetId"}, {NameRole, "name"},     {VoiceKeyRole, "voiceKey"}, {RateRole, "rate"},
            {PitchRole, "pitch"},   {EffectRole, "effect"}, {IntensityRole, "intensity"}};
}

QString PresetModel::add(const QString &name, const QString &voiceKey, int rate, int pitch, const QString &effect,
                         int intensity)
{
    VoicePreset p;
    p.name = name;
    p.voiceKey = voiceKey;
    p.rate = rate;
    p.pitch = pitch;
    p.effect = effect;
    p.effectIntensity = intensity;
    return m_presets->add(p);
}

void PresetModel::update(const QString &id, const QString &name, const QString &voiceKey, int rate, int pitch,
                         const QString &effect, int intensity)
{
    VoicePreset p = m_presets->preset(id);
    if (p.id.isEmpty())
        return;
    p.name = name;
    p.voiceKey = voiceKey;
    p.rate = rate;
    p.pitch = pitch;
    p.effect = effect;
    p.effectIntensity = intensity;
    m_presets->update(p);
}

void PresetModel::remove(const QString &id)
{
    m_presets->remove(id);
}
