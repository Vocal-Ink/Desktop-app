#include "ui/VoicePickerDialog.h"

#include "app/AppContext.h"
#include "core/Settings.h"
#include "tts/TtsRegistry.h"
#include "ui/VoicePreview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum Column { ColFav, ColName, ColProvider, ColLanguage, ColGender, ColDetails, ColCount };
enum Role { VoiceRole = Qt::UserRole + 1, EngineRole, SearchResultRole };

QString languageLabel(const QString &code)
{
    if (code.isEmpty())
        return QObject::tr("Multilingual");
    const QLocale locale(QString(code).replace(QLatin1Char('-'), QLatin1Char('_')));
    if (locale.language() == QLocale::C)
        return code;
    const QString name = QLocale::languageToString(locale.language());
    if (code.contains(QLatin1Char('-')) || code.contains(QLatin1Char('_')))
        return QStringLiteral("%1 (%2)").arg(name, QLocale::territoryToString(locale.territory()));
    return name;
}
} // namespace

class VoiceFilterModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    QString text;
    QString engine;
    QString language;
    bool favoritesOnly = false;

    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QAbstractItemModel *m = sourceModel();
        const QModelIndex nameIdx = m->index(row, ColName, parent);
        if (!engine.isEmpty() && m->data(nameIdx, EngineRole).toString() != engine)
            return false;
        if (favoritesOnly && m->data(m->index(row, ColFav, parent)).toString().isEmpty())
            return false;
        if (!language.isEmpty() && m->data(m->index(row, ColLanguage, parent)).toString() != language)
            return false;
        if (text.isEmpty())
            return true;
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QString haystack;
        for (int c = ColName; c < ColCount; ++c)
            haystack += m->data(m->index(row, c, parent)).toString() + QLatin1Char(' ');
        for (const QString &w : words) {
            if (!haystack.contains(w, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }
};

VoicePickerDialog::VoicePickerDialog(AppContext *context, QWidget *parent)
    : QDialog(parent)
    , m_ctx(context)
    , m_preview(new VoicePreview(context, this))
    , m_model(new QStandardItemModel(0, ColCount, this))
    , m_proxy(new VoiceFilterModel(this))
    , m_table(new QTableView(this))
    , m_search(new QLineEdit(this))
    , m_provider(new QComboBox(this))
    , m_language(new QComboBox(this))
    , m_favoritesOnly(new QCheckBox(tr("Favorites only"), this))
    , m_notes(new QLabel(this))
    , m_previewText(new QLineEdit(tr("Hi! This is how I sound. Nice to meet you."), this))
    , m_previewButton(new QPushButton(tr("▶ Preview"), this))
    , m_favButton(new QPushButton(tr("☆ Favorite"), this))
    , m_useButton(new QPushButton(tr("Use this voice"), this))
    , m_addById(new QPushButton(tr("Add voice by ID…"), this))
{
    setWindowTitle(tr("Choose a voice"));
    resize(980, 640);

    m_model->setHorizontalHeaderLabels({QStringLiteral("★"), tr("Name"), tr("Provider"), tr("Language"),
                                        tr("Gender"), tr("Details")});
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAccessibleName(tr("Voices"));

    m_search->setPlaceholderText(tr("Search by name, language, accent, style…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search voices"));
    m_provider->setAccessibleName(tr("Provider"));
    m_language->setAccessibleName(tr("Language"));
    m_useButton->setProperty("primary", true);
    m_useButton->setDefault(true);
    m_notes->setWordWrap(true);
    m_notes->setProperty("hint", true);
    m_notes->setTextFormat(Qt::RichText);
    m_previewText->setAccessibleName(tr("Preview text"));

    auto *filters = new QHBoxLayout;
    filters->addWidget(m_search, 3);
    filters->addWidget(m_provider, 1);
    filters->addWidget(m_language, 1);
    filters->addWidget(m_favoritesOnly);

    auto *previewRow = new QHBoxLayout;
    previewRow->addWidget(new QLabel(tr("Preview:"), this));
    previewRow->addWidget(m_previewText, 1);
    previewRow->addWidget(m_previewButton);

    auto *refresh = new QPushButton(tr("Refresh"), this);
    auto *close = new QPushButton(tr("Close"), this);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_favButton);
    bottom->addWidget(m_addById);
    bottom->addWidget(refresh);
    bottom->addStretch(1);
    bottom->addWidget(close);
    bottom->addWidget(m_useButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(filters);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_notes);
    layout->addLayout(previewRow);
    layout->addLayout(bottom);

    connect(m_search, &QLineEdit::textChanged, this, &VoicePickerDialog::updateFilters);
    connect(m_search, &QLineEdit::returnPressed, this, &VoicePickerDialog::remoteSearch);
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        updateFilters();
        updateButtons();
    });
    connect(m_language, &QComboBox::currentIndexChanged, this, &VoicePickerDialog::updateFilters);
    connect(m_favoritesOnly, &QCheckBox::toggled, this, &VoicePickerDialog::updateFilters);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &VoicePickerDialog::updateButtons);
    connect(m_table, &QTableView::doubleClicked, this, &QDialog::accept);
    connect(m_useButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_favButton, &QPushButton::clicked, this, &VoicePickerDialog::toggleFavorite);
    connect(m_addById, &QPushButton::clicked, this, &VoicePickerDialog::addVoiceById);
    connect(refresh, &QPushButton::clicked, this, [this] { m_ctx->tts()->refreshAll(); });
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        if (m_previewPlaying) {
            m_preview->stop();
            return;
        }
        const Voice v = voiceAt(m_table->currentIndex());
        if (v.isValid())
            m_preview->play(v, m_previewText->text(), false);
    });
    connect(m_preview, &VoicePreview::playingChanged, this, [this](bool playing) {
        m_previewPlaying = playing;
        m_previewButton->setText(playing ? tr("\u25A0 Stop") : tr("\u25B6 Preview"));
    });
    connect(m_preview, &VoicePreview::failed, this, [this](const QString &msg) {
        QMessageBox::warning(this, tr("Preview failed"), msg);
    });
    connect(m_notes, &QLabel::linkActivated, this, [this](const QString &link) {
        emit openSettingsRequested(link);
        reject();
    });
    connect(m_ctx->tts(), &TtsRegistry::voicesChanged, this, [this] {
        // Coalesce bursts of updates from several engines.
        QTimer::singleShot(100, this, &VoicePickerDialog::reload);
    });
    for (TtsEngine *e : m_ctx->tts()->engines()) {
        connect(e, &TtsEngine::searchFinished, this, [this](const QString &, const QList<Voice> &results) {
            appendVoices(results, true);
            updateFilters();
        });
    }

    reload();
    m_search->setFocus();
}

void VoicePickerDialog::reload()
{
    const Voice selected = voiceAt(m_table->currentIndex());
    const QString currentKey = selected.isValid() ? selected.key() : m_ctx->currentVoice().key();
    const QString provider = m_provider->currentData().toString();
    const QString language = m_language->currentData().toString();

    m_model->removeRows(0, m_model->rowCount());
    appendVoices(m_ctx->tts()->allVoices(), false);

    // Provider filter: every engine, available or not.
    m_provider->blockSignals(true);
    m_provider->clear();
    m_provider->addItem(tr("All providers"), QString());
    for (TtsEngine *e : m_ctx->tts()->engines())
        m_provider->addItem(e->displayName() + (e->isAvailable() ? QString() : tr(" (not set up)")), e->id());
    m_provider->setCurrentIndex(qMax(0, m_provider->findData(provider)));
    m_provider->blockSignals(false);

    QSet<QString> languages;
    for (int r = 0; r < m_model->rowCount(); ++r)
        languages.insert(m_model->item(r, ColLanguage)->text());
    QStringList langs(languages.begin(), languages.end());
    langs.sort(Qt::CaseInsensitive);
    m_language->blockSignals(true);
    m_language->clear();
    m_language->addItem(tr("All languages"), QString());
    for (const QString &l : std::as_const(langs))
        m_language->addItem(l, l);
    m_language->setCurrentIndex(qMax(0, m_language->findData(language)));
    m_language->blockSignals(false);

    m_table->sortByColumn(ColName, Qt::AscendingOrder);
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(ColFav, 34);
    m_table->setColumnWidth(ColName, qMin(m_table->columnWidth(ColName), 260));
    updateFilters();

    // Re-select the previous/current voice.
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex idx = m_proxy->index(r, ColName);
        if (voiceAt(idx).key() == currentKey) {
            m_table->setCurrentIndex(idx);
            m_table->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            break;
        }
    }
    updateEngineNotes();
    updateButtons();
}

void VoicePickerDialog::appendVoices(const QList<Voice> &voices, bool fromSearch)
{
    const QStringList favorites = m_ctx->settings()->value(Keys::FavoriteVoices).toStringList();
    QSet<QString> existing;
    for (int r = 0; r < m_model->rowCount(); ++r)
        existing.insert(m_model->item(r, ColName)->data(VoiceRole).value<Voice>().key());
    for (const Voice &v : voices) {
        if (existing.contains(v.key()))
            continue;
        existing.insert(v.key());
        TtsEngine *engine = m_ctx->tts()->engine(v.engineId);
        auto *fav = new QStandardItem(favorites.contains(v.key()) ? QStringLiteral("★") : QString());
        auto *name = new QStandardItem(v.name);
        name->setData(QVariant::fromValue(v), VoiceRole);
        name->setData(v.engineId, EngineRole);
        name->setData(fromSearch, SearchResultRole);
        name->setToolTip(v.description.isEmpty() ? v.name : v.name + QStringLiteral("\n") + v.description);
        auto *provider = new QStandardItem(engine ? engine->displayName() : v.engineId);
        if (engine && engine->isLocal())
            provider->setToolTip(tr("Runs on this computer — free, private, works offline"));
        auto *lang = new QStandardItem(languageLabel(v.language));
        auto *gender = new QStandardItem(v.gender);
        auto *details = new QStandardItem(v.description);
        m_model->appendRow({fav, name, provider, lang, gender, details});
    }
}

void VoicePickerDialog::updateFilters()
{
    m_proxy->text = m_search->text().trimmed();
    m_proxy->engine = m_provider->currentData().toString();
    m_proxy->language = m_language->currentData().toString();
    m_proxy->favoritesOnly = m_favoritesOnly->isChecked();
    m_proxy->refresh();
    TtsEngine *e = m_ctx->tts()->engine(m_proxy->engine);
    if (e && e->supportsRemoteSearch())
        m_search->setPlaceholderText(tr("Search %1's catalogue and press Enter…").arg(e->displayName()));
    else
        m_search->setPlaceholderText(tr("Search by name, language, accent, style…"));
}

void VoicePickerDialog::remoteSearch()
{
    const QString q = m_search->text().trimmed();
    if (q.isEmpty())
        return;
    for (TtsEngine *e : m_ctx->tts()->engines()) {
        if (e->supportsRemoteSearch() && e->isAvailable()
            && (m_proxy->engine.isEmpty() || m_proxy->engine == e->id()))
            e->searchVoices(q);
    }
}

void VoicePickerDialog::updateEngineNotes()
{
    QStringList notes;
    for (TtsEngine *e : m_ctx->tts()->engines()) {
        if (e->isAvailable())
            continue;
        const QString reason = e->unavailableReason();
        notes << QStringLiteral("<b>%1</b>: %2 <a href=\"voices\">Set up…</a>")
                     .arg(e->displayName().toHtmlEscaped(),
                          (reason.isEmpty() ? tr("not set up yet.") : reason).toHtmlEscaped());
    }
    m_notes->setText(notes.join(QStringLiteral("<br>")));
    m_notes->setVisible(!notes.isEmpty());
}

void VoicePickerDialog::updateButtons()
{
    const Voice v = voiceAt(m_table->currentIndex());
    m_useButton->setEnabled(v.isValid());
    m_previewButton->setEnabled(v.isValid());
    m_favButton->setEnabled(v.isValid());
    const QStringList favorites = m_ctx->settings()->value(Keys::FavoriteVoices).toStringList();
    m_favButton->setText(v.isValid() && favorites.contains(v.key()) ? tr("★ Unfavorite") : tr("☆ Favorite"));

    bool customIds = false;
    for (TtsEngine *e : m_ctx->tts()->engines())
        customIds = customIds || (e->supportsCustomVoiceIds() && e->isAvailable());
    m_addById->setVisible(customIds);
}

Voice VoicePickerDialog::voiceAt(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return {};
    const QModelIndex src = m_proxy->mapToSource(proxyIndex);
    return m_model->item(src.row(), ColName)->data(VoiceRole).value<Voice>();
}

Voice VoicePickerDialog::selectedVoice() const
{
    return voiceAt(m_table->currentIndex());
}

void VoicePickerDialog::toggleFavorite()
{
    const QModelIndex idx = m_table->currentIndex();
    const Voice v = voiceAt(idx);
    if (!v.isValid())
        return;
    QStringList favorites = m_ctx->settings()->value(Keys::FavoriteVoices).toStringList();
    const bool nowFav = !favorites.contains(v.key());
    if (nowFav)
        favorites << v.key();
    else
        favorites.removeAll(v.key());
    m_ctx->settings()->setValue(Keys::FavoriteVoices, favorites);
    const QModelIndex src = m_proxy->mapToSource(idx);
    m_model->item(src.row(), ColFav)->setText(nowFav ? QStringLiteral("★") : QString());
    updateButtons();
}

void VoicePickerDialog::addVoiceById()
{
    QList<TtsEngine *> candidates;
    QStringList names;
    for (TtsEngine *e : m_ctx->tts()->engines()) {
        if (e->supportsCustomVoiceIds() && e->isAvailable()) {
            candidates << e;
            names << e->displayName();
        }
    }
    if (candidates.isEmpty())
        return;
    bool ok = true;
    int which = 0;
    if (candidates.size() > 1) {
        const QString choice = QInputDialog::getItem(this, tr("Add voice by ID"), tr("Provider:"), names, 0, false, &ok);
        if (!ok)
            return;
        which = int(names.indexOf(choice));
    }
    TtsEngine *engine = candidates.value(which);
    if (!engine)
        return;
    const QString id = QInputDialog::getText(this, tr("Add voice by ID"),
                                             tr("Paste the %1 voice ID (from the voice's page or URL):").arg(engine->displayName()),
                                             QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || id.isEmpty())
        return;
    const QString name = QInputDialog::getText(this, tr("Add voice by ID"), tr("A name for this voice:"),
                                               QLineEdit::Normal, id, &ok).trimmed();
    if (!ok)
        return;
    engine->addCustomVoice(id, name.isEmpty() ? id : name);
}
