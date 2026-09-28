#include "ui/SetupWizard.h"

#include "app/AppContext.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "models/ModelManager.h"
#include "platform/VirtualAudio.h"
#include "ui/ModelListWidget.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QLabel *text(const QString &html, QWidget *parent)
{
    auto *l = new QLabel(html, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    return l;
}

void fillOutputs(QComboBox *combo, const QByteArray &current)
{
    combo->clear();
    combo->addItem(QObject::tr("System default"), QByteArray());
    const auto outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &d : outputs) {
        QString label = d.description();
        if (VirtualAudio::looksLikeVirtualCable(label))
            label += QObject::tr("  (virtual cable)");
        combo->addItem(label, d.id());
    }
    combo->setCurrentIndex(qMax(0, combo->findData(current)));
}

} // namespace

SetupWizard::SetupWizard(AppContext *context, QWidget *parent)
    : QWizard(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Set up Vocal Ink"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setPixmap(QWizard::LogoPixmap, QPixmap(QStringLiteral(":/icons/app-256.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    resize(820, 620);

    addPage(welcomePage());
    addPage(outputPage());
    addPage(voicePage());
    addPage(sttPage());
    addPage(cloudPage());
    addPage(donePage());
}

QWizardPage *SetupWizard::welcomePage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("Welcome to Vocal Ink"));
    page->setSubTitle(tr("Your words, in a voice you choose — in calls, games and on stream."));
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(text(tr("<p>Vocal Ink speaks for you:</p>"
                              "<ul>"
                              "<li><b>Type</b> a message, or <b>speak</b> (even quietly) and let it be written down for you.</li>"
                              "<li>It is said aloud in the <b>voice you pick</b> — free voices on your computer, "
                              "or Azure, ElevenLabs, Fish Audio and OpenAI voices with your own key.</li>"
                              "<li>Other apps hear it as a <b>microphone</b>, and it can show <b>captions on your stream</b>.</li>"
                              "</ul>"
                              "<p>This assistant takes about two minutes. You can skip anything and change it later in Settings.</p>"),
                           page));
    layout->addStretch(1);
    return page;
}

QWizardPage *SetupWizard::outputPage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("Where should your voice go?"));
    page->setSubTitle(tr("To be heard in Discord, games or OBS, Vocal Ink plays into a virtual audio cable."));
    auto *layout = new QVBoxLayout(page);

    auto *combo = new QComboBox(page);
    combo->setAccessibleName(tr("Voice output device"));
    QByteArray current = m_ctx->settings()->value(Keys::OutputDevice).toByteArray();
    if (current.isEmpty())
        current = VirtualAudio::detectVirtualCableOutput(); // pre-select an installed cable
    fillOutputs(combo, current);
    auto *detected = new QLabel(page);
    detected->setWordWrap(true);
    auto updateDetected = [detected] {
        detected->setText(VirtualAudio::detectVirtualCableOutput().isEmpty()
                              ? tr("⚠ No virtual cable found yet. Without one, only you will hear the voice.")
                              : tr("✔ A virtual cable is installed."));
    };
    updateDetected();

    auto *form = new QFormLayout;
    form->addRow(tr("Voice output:"), combo);
    layout->addLayout(form);
    layout->addWidget(detected);
    auto *monitor = new QCheckBox(tr("Also play on my speakers/headphones so I hear myself"), page);
    monitor->setChecked(m_ctx->settings()->flag(Keys::MonitorEnabled));
    layout->addWidget(monitor);
    layout->addWidget(text(VirtualAudio::setupInstructions(), page));

    if (VirtualAudio::canCreateVirtualMic()) {
        auto *create = new QPushButton(tr("Create virtual microphone"), page);
        layout->addWidget(create, 0, Qt::AlignLeft);
        connect(create, &QPushButton::clicked, page, [page, combo, updateDetected] {
            QString error;
            if (!VirtualAudio::createVirtualMic(&error)) {
                QMessageBox::warning(page, tr("Virtual microphone"), error);
                return;
            }
            fillOutputs(combo, VirtualAudio::detectVirtualCableOutput());
            updateDetected();
        });
    }
    auto *rescan = new QPushButton(tr("I installed a cable — look again"), page);
    layout->addWidget(rescan, 0, Qt::AlignLeft);
    connect(rescan, &QPushButton::clicked, page, [combo, updateDetected] {
        fillOutputs(combo, VirtualAudio::detectVirtualCableOutput());
        updateDetected();
    });
    layout->addStretch(1);

    connect(combo, &QComboBox::currentIndexChanged, page, [this, combo] {
        m_ctx->settings()->setValue(Keys::OutputDevice, combo->currentData().toByteArray());
        m_ctx->applyAudioRouting();
    });
    connect(monitor, &QCheckBox::toggled, page, [this](bool on) {
        m_ctx->settings()->setValue(Keys::MonitorEnabled, on);
        m_ctx->applyAudioRouting();
    });
    // Persist the pre-selection even if the user doesn't touch the combo.
    m_ctx->settings()->setValue(Keys::OutputDevice, combo->currentData().toByteArray());
    return page;
}

QWizardPage *SetupWizard::voicePage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("Get a free natural voice"));
    page->setSubTitle(tr("Piper voices run on your computer: free, private and they work offline."));
    auto *layout = new QVBoxLayout(page);
    auto *list = new ModelListWidget(ModelListWidget::Kind::PiperVoices, m_ctx, page);
    list->setRecommendedOnly(true);
    auto *download = new QPushButton(tr("Download the recommended voice (about 90 MB)"), page);
    download->setProperty("primary", true);
    connect(download, &QPushButton::clicked, list, &ModelListWidget::downloadRecommended);
    auto *more = new QCheckBox(tr("Show voices in all languages"), page);
    connect(more, &QCheckBox::toggled, list, [list](bool on) { list->setRecommendedOnly(!on); });

    layout->addWidget(download, 0, Qt::AlignLeft);
    layout->addWidget(more);
    layout->addWidget(list, 1);
    layout->addWidget(text(tr("<i>You can also use your computer's built-in voices, or add cloud voices on the next pages.</i>"), page));
    return page;
}

QWizardPage *SetupWizard::sttPage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("Talk instead of typing (optional)"));
    page->setSubTitle(tr("Speech recognition runs on your computer. It works with quiet or unclear speech too — "
                         "you can check the text before it's spoken."));
    auto *layout = new QVBoxLayout(page);
    auto *list = new ModelListWidget(ModelListWidget::Kind::Whisper, m_ctx, page);
    auto *download = new QPushButton(tr("Download the recommended model (about 60 MB)"), page);
    download->setProperty("primary", true);
    connect(download, &QPushButton::clicked, list, &ModelListWidget::downloadRecommended);

    auto *ptt = new QRadioButton(tr("Push-to-talk: hold a button or shortcut while speaking (most reliable)"), page);
    auto *toggle = new QRadioButton(tr("Toggle: press once to start and again to stop"), page);
    auto *vad = new QRadioButton(tr("Hands-free: Vocal Ink notices when you talk"), page);
    const QString mode = m_ctx->settings()->string(Keys::SttMode);
    (mode == QLatin1String("vad") ? vad : mode == QLatin1String("toggle") ? toggle : ptt)->setChecked(true);
    auto store = [this](const char *value) {
        return [this, value](bool on) {
            if (!on)
                return;
            m_ctx->settings()->setValue(Keys::SttMode, QString::fromLatin1(value));
            m_ctx->applySttSettings();
        };
    };
    connect(ptt, &QRadioButton::toggled, page, store("ptt"));
    connect(toggle, &QRadioButton::toggled, page, store("toggle"));
    connect(vad, &QRadioButton::toggled, page, store("vad"));

    layout->addWidget(download, 0, Qt::AlignLeft);
    layout->addWidget(list, 1);
    layout->addWidget(ptt);
    layout->addWidget(toggle);
    layout->addWidget(vad);
    return page;
}

QWizardPage *SetupWizard::cloudPage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("Cloud voices (optional)"));
    page->setSubTitle(tr("Paste a key to unlock that provider's voices. Leave empty to skip."));
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;
    auto addKey = [this, page, form](const QString &label, const QString &secretName, const QString &url) {
        auto *edit = new QLineEdit(m_ctx->secrets()->get(secretName), page);
        edit->setEchoMode(QLineEdit::Password);
        edit->setPlaceholderText(tr("API key"));
        edit->setAccessibleName(label);
        connect(edit, &QLineEdit::editingFinished, page, [this, edit, secretName] {
            m_ctx->secrets()->set(secretName, edit->text());
        });
        form->addRow(label, edit);
        form->addRow(QString(), text(QStringLiteral("<a href=\"%1\">%2</a>").arg(url, tr("Get a key")), page));
    };
    addKey(tr("ElevenLabs:"), Secrets::ElevenLabs, QStringLiteral("https://elevenlabs.io/app/settings/api-keys"));
    addKey(tr("Fish Audio:"), Secrets::FishAudio, QStringLiteral("https://fish.audio/app/api-keys/"));
    addKey(tr("OpenAI:"), Secrets::OpenAi, QStringLiteral("https://platform.openai.com/api-keys"));
    addKey(tr("Microsoft Azure:"), Secrets::Azure, QStringLiteral("https://portal.azure.com/#create/Microsoft.CognitiveServicesSpeechServices"));
    layout->addLayout(form);
    layout->addWidget(text(tr("<i>Azure also needs its region (Settings → Voices). Keys are kept in your system's "
                              "password manager.</i>"),
                           page));
    layout->addStretch(1);
    return page;
}

QWizardPage *SetupWizard::donePage()
{
    auto *page = new QWizardPage;
    page->setTitle(tr("You're ready"));
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(text(tr("<p>Handy keys:</p>"
                              "<table cellpadding=\"4\">"
                              "<tr><td><b>Enter</b></td><td>speak what you typed</td></tr>"
                              "<tr><td><b>Esc</b></td><td>stop speaking</td></tr>"
                              "<tr><td><b>↑</b></td><td>bring back an earlier message</td></tr>"
                              "<tr><td><b>Alt+1…9</b></td><td>say a quick phrase</td></tr>"
                              "<tr><td><b>Ctrl+Alt+Space</b></td><td>hold to talk (works in any app)</td></tr>"
                              "<tr><td><b>Ctrl+Alt+T</b></td><td>quick-type box over games</td></tr>"
                              "</table>"
                              "<p>Streaming? See <b>Settings → OBS &amp; stream</b> for live captions.</p>"),
                           page));
    layout->addStretch(1);
    return page;
}

void SetupWizard::accept()
{
    m_ctx->settings()->setValue(Keys::FirstRunDone, true);
    m_ctx->applyAll();
    QWizard::accept();
}
