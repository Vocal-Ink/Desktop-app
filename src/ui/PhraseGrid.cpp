#include "ui/PhraseGrid.h"

#include "tts/TtsRegistry.h"
#include "ui/FlowLayout.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

namespace {
QString elide(const QString &text, int max = 42)
{
    return text.size() <= max ? text : text.left(max - 1).trimmed() + QStringLiteral("…");
}
} // namespace

PhraseGrid::PhraseGrid(PhraseStore *store, TtsRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_registry(registry)
    , m_flow(new FlowLayout(this, 8))
{
    setAccessibleName(tr("Quick phrases"));
    connect(m_store, &PhraseStore::changed, this, &PhraseGrid::rebuild);
    rebuild();
}

int PhraseGrid::heightForWidth(int width) const
{
    return m_flow->heightForWidth(width);
}

void PhraseGrid::rebuild()
{
    while (QLayoutItem *item = m_flow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<Phrase> &list = m_store->phrases();
    for (int i = 0; i < list.size(); ++i) {
        const Phrase &p = list.at(i);
        auto *button = new QPushButton(elide(p.text), this);
        button->setProperty("phrase", true);
        button->setCursor(Qt::PointingHandCursor);
        QString tip = p.text;
        if (i < 9)
            tip += tr("\nShortcut in this window: Alt+%1").arg(i + 1);
        if (!p.hotkey.isEmpty())
            tip += tr("\nSystem-wide shortcut: %1").arg(QKeySequence(p.hotkey).toString(QKeySequence::NativeText));
        tip += tr("\nRight-click to edit");
        button->setToolTip(tip);
        button->setAccessibleName(tr("Say: %1").arg(p.text));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QPushButton::clicked, this, [this, i] { emit phraseActivated(i); });
        connect(button, &QWidget::customContextMenuRequested, this, [this, i, button](const QPoint &pos) {
            showMenu(i, button->mapToGlobal(pos));
        });
        m_flow->addWidget(button);
    }
    auto *add = new QToolButton(this);
    add->setText(tr("+ Add phrase"));
    add->setAccessibleName(tr("Add a quick phrase"));
    connect(add, &QToolButton::clicked, this, [this] { addPhrase(); });
    m_flow->addWidget(add);
    updateGeometry();
}

void PhraseGrid::showMenu(int index, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Say it"), this, [this, index] { emit phraseActivated(index); });
    menu.addAction(tr("Edit…"), this, [this, index] { editPhrase(index); });
    menu.addSeparator();
    QAction *left = menu.addAction(tr("Move earlier"), this, [this, index] { m_store->move(index, index - 1); });
    left->setEnabled(index > 0);
    QAction *right = menu.addAction(tr("Move later"), this, [this, index] { m_store->move(index, index + 1); });
    right->setEnabled(index < m_store->phrases().size() - 1);
    menu.addSeparator();
    menu.addAction(tr("Delete"), this, [this, index] {
        const QString text = m_store->phrases().value(index).text;
        if (QMessageBox::question(this, tr("Delete phrase"), tr("Delete “%1”?").arg(elide(text, 60)))
            == QMessageBox::Yes)
            m_store->removeAt(index);
    });
    menu.exec(globalPos);
}

void PhraseGrid::addPhrase(const QString &initialText)
{
    Phrase p;
    p.text = initialText;
    PhraseEditDialog dlg(p, m_registry, this);
    dlg.setWindowTitle(tr("New quick phrase"));
    if (dlg.exec() == QDialog::Accepted && !dlg.phrase().text.isEmpty())
        m_store->add(dlg.phrase());
}

void PhraseGrid::editPhrase(int index)
{
    if (index < 0 || index >= m_store->phrases().size())
        return;
    PhraseEditDialog dlg(m_store->phrases().at(index), m_registry, this);
    dlg.setWindowTitle(tr("Edit quick phrase"));
    if (dlg.exec() == QDialog::Accepted && !dlg.phrase().text.isEmpty())
        m_store->update(index, dlg.phrase());
}

// --- PhraseEditDialog ------------------------------------------------------------

PhraseEditDialog::PhraseEditDialog(const Phrase &phrase, TtsRegistry *registry, QWidget *parent)
    : QDialog(parent)
    , m_text(new QPlainTextEdit(phrase.text, this))
    , m_hotkey(new QKeySequenceEdit(QKeySequence::fromString(phrase.hotkey, QKeySequence::PortableText), this))
    , m_voice(new QComboBox(this))
{
    m_text->setAccessibleName(tr("Phrase text"));
    m_text->setMinimumHeight(80);
    m_hotkey->setAccessibleName(tr("System-wide shortcut"));

    m_voice->addItem(tr("Use the current voice"), QString());
    if (registry) {
        QList<Voice> voices = registry->allVoices();
        std::sort(voices.begin(), voices.end(), [](const Voice &a, const Voice &b) {
            return a.engineId == b.engineId ? a.name.localeAwareCompare(b.name) < 0 : a.engineId < b.engineId;
        });
        for (const Voice &v : voices) {
            TtsEngine *e = registry->engine(v.engineId);
            m_voice->addItem(QStringLiteral("%1 — %2").arg(v.name, e ? e->displayName() : v.engineId), v.key());
        }
        if (!phrase.voiceKey.isEmpty()) {
            int idx = m_voice->findData(phrase.voiceKey);
            if (idx < 0) {
                m_voice->addItem(phrase.voiceKey, phrase.voiceKey);
                idx = m_voice->count() - 1;
            }
            m_voice->setCurrentIndex(idx);
        }
    }

    auto *clear = new QToolButton(this);
    clear->setText(tr("Clear"));
    connect(clear, &QToolButton::clicked, m_hotkey, &QKeySequenceEdit::clear);
    auto *hotkeyRow = new QHBoxLayout;
    hotkeyRow->addWidget(m_hotkey, 1);
    hotkeyRow->addWidget(clear);

    auto *form = new QFormLayout;
    form->addRow(tr("Say:"), m_text);
    form->addRow(tr("Shortcut (works in any app):"), hotkeyRow);
    form->addRow(tr("Voice:"), m_voice);
    auto *hint = new QLabel(tr("Tip: use shortcuts with Ctrl+Alt so they don't clash with games."), this);
    hint->setProperty("hint", true);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    resize(480, 260);
    m_text->setFocus();
}

Phrase PhraseEditDialog::phrase() const
{
    Phrase p;
    p.text = m_text->toPlainText().trimmed();
    p.hotkey = m_hotkey->keySequence().toString(QKeySequence::PortableText);
    p.voiceKey = m_voice->currentData().toString();
    return p;
}
