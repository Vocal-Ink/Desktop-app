#include "ui/ModelListWidget.h"

#include "app/AppContext.h"
#include "core/Settings.h"
#include "models/ModelManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString kRuntimeTask = QStringLiteral("piper-runtime");

QString sizeText(qint64 bytes)
{
    return bytes > 0 ? QLocale().formattedDataSize(bytes, 0) : QString();
}
} // namespace

ModelListWidget::ModelListWidget(Kind kind, AppContext *context, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_ctx(context)
    , m_tree(new QTreeWidget(this))
    , m_status(new QLabel(this))
{
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Name"), tr("Details"), QString()});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setAccessibleName(kind == Kind::Whisper ? tr("Speech recognition models") : tr("Piper voices"));
    m_status->setProperty("hint", true);
    m_status->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (kind == Kind::PiperVoices) {
        auto *runtimeRow = new QHBoxLayout;
        m_runtimeLabel = new QLabel(this);
        m_runtimeLabel->setWordWrap(true);
        m_runtimeButton = new QPushButton(this);
        m_runtimeProgress = new QProgressBar(this);
        m_runtimeProgress->setMaximumWidth(160);
        m_runtimeProgress->hide();
        runtimeRow->addWidget(m_runtimeLabel, 1);
        runtimeRow->addWidget(m_runtimeProgress);
        runtimeRow->addWidget(m_runtimeButton);
        layout->addLayout(runtimeRow);
        connect(m_runtimeButton, &QPushButton::clicked, this, [this] {
            if (m_ctx->models()->isDownloading(kRuntimeTask))
                m_ctx->models()->cancel(kRuntimeTask);
            else
                m_ctx->models()->downloadPiperRuntime();
            refreshRuntime();
        });

        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(tr("Filter voices by language or name (e.g. “german”, “en_GB”)"));
        m_filter->setClearButtonEnabled(true);
        m_filter->setAccessibleName(tr("Filter Piper voices"));
        connect(m_filter, &QLineEdit::textChanged, this, &ModelListWidget::applyFilter);
        layout->addWidget(m_filter);

        connect(m_ctx->models(), &ModelManager::piperCatalogChanged, this, &ModelListWidget::rebuild);
        connect(m_ctx->models(), &ModelManager::piperCatalogError, this, [this](const QString &msg) {
            m_status->setText(tr("Could not load the voice list: %1").arg(msg));
        });
        m_ctx->models()->refreshPiperCatalog();
    }
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_status);

    ModelManager *mm = m_ctx->models();
    connect(mm, &ModelManager::downloadProgress, this, [this](const QString &task, qint64 got, qint64 total) {
        if (task == kRuntimeTask && m_runtimeProgress) {
            m_runtimeProgress->show();
            m_runtimeProgress->setMaximum(total > 0 ? 1000 : 0);
            m_runtimeProgress->setValue(total > 0 ? int(got * 1000 / total) : 0);
            return;
        }
        const auto it = m_rows.constFind(task);
        if (it == m_rows.cend())
            return;
        it->progress->show();
        it->progress->setMaximum(total > 0 ? 1000 : 0);
        it->progress->setValue(total > 0 ? int(got * 1000 / total) : 0);
        it->action->setText(tr("Cancel"));
    });
    connect(mm, &ModelManager::downloadFinished, this, [this](const QString &task) {
        if (task == kRuntimeTask)
            refreshRuntime();
        else
            refreshRow(task);
        m_status->setText(tr("Download complete."));
        emit installedChanged();
    });
    connect(mm, &ModelManager::downloadFailed, this, [this](const QString &task, const QString &error) {
        if (task == kRuntimeTask)
            refreshRuntime();
        else
            refreshRow(task);
        m_status->setText(tr("Download failed: %1").arg(error));
    });
    connect(mm, &ModelManager::installedChanged, this, [this] {
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
            refreshRow(it.key());
        refreshRuntime();
    });
    if (kind == Kind::Whisper) {
        connect(m_ctx->settings(), &Settings::changed, this, [this](const QString &key) {
            if (key == QLatin1String(Keys::WhisperModel)) {
                for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
                    refreshRow(it.key());
            }
        });
    }
    rebuild();
}

void ModelListWidget::setRecommendedOnly(bool only)
{
    m_recommendedOnly = only;
    if (m_filter)
        m_filter->setVisible(!only);
    rebuild();
}

void ModelListWidget::rebuild()
{
    m_tree->clear();
    m_rows.clear();
    if (m_kind == Kind::Whisper) {
        const QList<WhisperModelInfo> catalog = ModelManager::whisperCatalog();
        for (const WhisperModelInfo &m : catalog) {
            if (m_recommendedOnly && !m.recommended)
                continue;
            const QString details = QStringLiteral("%1 • %2%3")
                                        .arg(m.description, sizeText(m.approxBytes),
                                             m.multilingual ? tr(" • many languages") : tr(" • English"));
            addRow(QStringLiteral("whisper:") + m.file, m.title, details, m.recommended);
        }
    } else {
        refreshRuntime();
        QList<PiperVoiceInfo> voices = m_ctx->models()->piperCatalog();
        const QString myLang = QLocale().name();         // e.g. en_US
        const QString myLangShort = myLang.section(QLatin1Char('_'), 0, 0);
        std::stable_sort(voices.begin(), voices.end(), [&](const PiperVoiceInfo &a, const PiperVoiceInfo &b) {
            auto rank = [&](const PiperVoiceInfo &v) {
                if (v.languageCode == myLang)
                    return 0;
                if (v.languageCode.startsWith(myLangShort))
                    return 1;
                return 2;
            };
            const int ra = rank(a), rb = rank(b);
            return ra != rb ? ra < rb : a.key < b.key;
        });
        const QString recommended = ModelManager::recommendedPiperVoice();
        for (const PiperVoiceInfo &v : std::as_const(voices)) {
            const bool rec = v.key == recommended;
            if (m_recommendedOnly && !rec)
                continue;
            QString details = QStringLiteral("%1 • %2 • %3").arg(v.languageName, v.quality, sizeText(v.totalBytes()));
            if (v.numSpeakers > 1)
                details += tr(" • %n speakers", nullptr, v.numSpeakers);
            addRow(QStringLiteral("piper-voice:") + v.key, v.key, details, rec);
        }
        if (voices.isEmpty())
            m_status->setText(tr("Loading the Piper voice list…"));
    }
    applyFilter();
}

void ModelListWidget::addRow(const QString &taskId, const QString &title, const QString &details, bool recommended)
{
    Row row;
    row.item = new QTreeWidgetItem(m_tree);
    row.item->setText(0, recommended ? title + tr("  ★ recommended") : title);
    row.item->setText(1, details);
    row.item->setToolTip(1, details);

    auto *cell = new QWidget(m_tree);
    auto *h = new QHBoxLayout(cell);
    h->setContentsMargins(2, 2, 2, 2);
    row.progress = new QProgressBar(cell);
    row.progress->setMaximumWidth(120);
    row.progress->hide();
    row.action = new QPushButton(cell);
    h->addWidget(row.progress);
    if (m_kind == Kind::Whisper) {
        row.use = new QPushButton(tr("Use"), cell);
        h->addWidget(row.use);
        connect(row.use, &QPushButton::clicked, this, [this, taskId] {
            m_ctx->settings()->setValue(Keys::WhisperModel, taskId.section(QLatin1Char(':'), 1));
        });
    }
    h->addWidget(row.action);
    m_tree->setItemWidget(row.item, 2, cell);
    connect(row.action, &QPushButton::clicked, this, [this, taskId] { onAction(taskId); });
    m_rows.insert(taskId, row);
    refreshRow(taskId);
}

bool ModelListWidget::isInstalled(const QString &taskId) const
{
    const QString name = taskId.section(QLatin1Char(':'), 1);
    if (taskId.startsWith(QLatin1String("whisper:")))
        return m_ctx->models()->isWhisperModelInstalled(name);
    return m_ctx->models()->isPiperVoiceInstalled(name);
}

bool ModelListWidget::hasInstalled() const
{
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        if (isInstalled(it.key()))
            return true;
    }
    return false;
}

void ModelListWidget::refreshRow(const QString &taskId)
{
    const auto it = m_rows.constFind(taskId);
    if (it == m_rows.cend())
        return;
    const Row &row = *it;
    const bool downloading = m_ctx->models()->isDownloading(taskId);
    const bool installed = isInstalled(taskId);
    row.progress->setVisible(downloading);
    if (downloading)
        row.action->setText(tr("Cancel"));
    else if (installed)
        row.action->setText(tr("Remove"));
    else
        row.action->setText(tr("Download"));
    row.action->setProperty("danger", installed && !downloading);
    row.action->style()->unpolish(row.action);
    row.action->style()->polish(row.action);
    row.action->setAccessibleName(row.action->text() + QLatin1Char(' ') + row.item->text(0));

    if (row.use) {
        const bool inUse = m_ctx->settings()->string(Keys::WhisperModel) == taskId.section(QLatin1Char(':'), 1);
        row.use->setVisible(installed && !downloading);
        row.use->setEnabled(!inUse);
        row.use->setText(inUse ? tr("✔ In use") : tr("Use"));
    }
}

void ModelListWidget::refreshRuntime()
{
    if (!m_runtimeLabel)
        return;
    ModelManager *mm = m_ctx->models();
    const bool downloading = mm->isDownloading(kRuntimeTask);
    const bool installed = mm->isPiperRuntimeInstalled();
    const bool supported = !ModelManager::piperRuntimeUrl().isEmpty();
    m_runtimeProgress->setVisible(downloading);
    if (installed) {
        m_runtimeLabel->setText(tr("✔ Piper voice engine is installed. Download voices below."));
        m_runtimeButton->hide();
    } else if (!supported) {
        m_runtimeLabel->setText(tr("There is no ready-made Piper build for this computer. Install Piper yourself and "
                                   "set its path under Advanced."));
        m_runtimeButton->hide();
    } else {
        m_runtimeLabel->setText(tr("Piper needs its voice engine (about 25 MB) once, then voices below."));
        m_runtimeButton->show();
        m_runtimeButton->setText(downloading ? tr("Cancel") : tr("Download voice engine"));
    }
}

void ModelListWidget::onAction(const QString &taskId)
{
    ModelManager *mm = m_ctx->models();
    const QString name = taskId.section(QLatin1Char(':'), 1);
    if (mm->isDownloading(taskId)) {
        mm->cancel(taskId);
    } else if (isInstalled(taskId)) {
        if (QMessageBox::question(this, tr("Remove"), tr("Delete %1 from this computer?").arg(name)) != QMessageBox::Yes)
            return;
        if (taskId.startsWith(QLatin1String("whisper:")))
            mm->removeWhisperModel(name);
        else
            mm->removePiperVoice(name);
        emit installedChanged();
    } else {
        if (taskId.startsWith(QLatin1String("whisper:"))) {
            mm->downloadWhisperModel(name);
            // Downloading a model when none is usable makes it the active one.
            if (!mm->isWhisperModelInstalled(m_ctx->settings()->string(Keys::WhisperModel)))
                m_ctx->settings()->setValue(Keys::WhisperModel, name);
        } else {
            if (!mm->isPiperRuntimeInstalled() && !mm->isDownloading(kRuntimeTask))
                mm->downloadPiperRuntime();
            mm->downloadPiperVoice(name);
        }
        m_status->setText(tr("Downloading %1…").arg(name));
    }
    refreshRow(taskId);
    refreshRuntime();
}

void ModelListWidget::downloadRecommended()
{
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        if (it->item->text(0).contains(QStringLiteral("★")) && !isInstalled(it.key())
            && !m_ctx->models()->isDownloading(it.key()))
            onAction(it.key());
    }
    if (m_kind == Kind::PiperVoices && !m_ctx->models()->isPiperRuntimeInstalled()
        && !m_ctx->models()->isDownloading(kRuntimeTask))
        m_ctx->models()->downloadPiperRuntime();
    refreshRuntime();
}

void ModelListWidget::applyFilter()
{
    const QString f = m_filter ? m_filter->text().trimmed() : QString();
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        const bool match = f.isEmpty() || it->item->text(0).contains(f, Qt::CaseInsensitive)
            || it->item->text(1).contains(f, Qt::CaseInsensitive);
        it->item->setHidden(!match);
    }
}
