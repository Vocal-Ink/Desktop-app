#include "ui/SettingsDialog.h"

#include "app/AppContext.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "core/SpeechQueue.h"
#include "obs/ObsIntegration.h"
#include "obs/OverlayServer.h"
#include "platform/GlobalHotkeys.h"
#include "platform/VirtualAudio.h"
#include "stt/SttEngines.h"
#include "tts/TtsRegistry.h"
#include "ui/ModelListWidget.h"

#include <QApplication>
#include <QAudioDevice>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("hint", true);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    return l;
}

QWidget *scrollable(QWidget *content)
{
    auto *area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    return area;
}

const QList<QPair<QString, QVariant>> &languages()
{
    static const QList<QPair<QString, QVariant>> list = {
        {QObject::tr("Detect automatically"), QStringLiteral("auto")},
        {QStringLiteral("English"), QStringLiteral("en")},
        {QStringLiteral("Español"), QStringLiteral("es")},
        {QStringLiteral("Français"), QStringLiteral("fr")},
        {QStringLiteral("Deutsch"), QStringLiteral("de")},
        {QStringLiteral("Italiano"), QStringLiteral("it")},
        {QStringLiteral("Português"), QStringLiteral("pt")},
        {QStringLiteral("Nederlands"), QStringLiteral("nl")},
        {QStringLiteral("Polski"), QStringLiteral("pl")},
        {QStringLiteral("Türkçe"), QStringLiteral("tr")},
        {QStringLiteral("Русский"), QStringLiteral("ru")},
        {QStringLiteral("Українська"), QStringLiteral("uk")},
        {QStringLiteral("العربية"), QStringLiteral("ar")},
        {QStringLiteral("हिन्दी"), QStringLiteral("hi")},
        {QStringLiteral("日本語"), QStringLiteral("ja")},
        {QStringLiteral("한국어"), QStringLiteral("ko")},
        {QStringLiteral("中文"), QStringLiteral("zh")},
    };
    return list;
}

} // namespace

SettingsDialog::SettingsDialog(AppContext *context, QWidget *parent)
    : QDialog(parent)
    , m_ctx(context)
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(tr("Vocal Ink Settings"));
    resize(860, 680);

    m_tabs->addTab(buildGeneral(), tr("General"));
    m_tabs->addTab(buildAudio(), tr("Audio"));
    m_tabs->addTab(buildVoices(), tr("Voices"));
    m_tabs->addTab(buildStt(), tr("Speech recognition"));
    m_tabs->addTab(buildObs(), tr("OBS && stream"));
    m_tabs->addTab(buildHotkeys(), tr("Shortcuts"));
    m_tabs->addTab(buildText(), tr("Text"));
    m_tabs->setDocumentMode(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
}

void SettingsDialog::showPage(const QString &page)
{
    static const QStringList pages = {QStringLiteral("general"), QStringLiteral("audio"), QStringLiteral("voices"),
                                      QStringLiteral("stt"),     QStringLiteral("obs"),   QStringLiteral("hotkeys"),
                                      QStringLiteral("text")};
    const int idx = int(pages.indexOf(page));
    if (idx >= 0)
        m_tabs->setCurrentIndex(idx);
}

// --- binding helpers ---------------------------------------------------------

QCheckBox *SettingsDialog::check(const QString &label, const char *key, std::function<void()> apply)
{
    auto *box = new QCheckBox(label);
    box->setChecked(m_ctx->settings()->flag(key));
    connect(box, &QCheckBox::toggled, this, [this, key, apply](bool on) {
        m_ctx->settings()->setValue(key, on);
        if (apply)
            apply();
    });
    return box;
}

QWidget *SettingsDialog::sliderRow(const char *key, int min, int max, const QString &suffix,
                                   const QString &accessibleName, std::function<void()> apply)
{
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    QSlider *s = slider(key, min, max, apply);
    s->setAccessibleName(accessibleName);
    auto *value = new QLabel(row);
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("-100 %")));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto show = [value, suffix](int v) { value->setText(QString::number(v) + suffix); };
    show(s->value());
    connect(s, &QSlider::valueChanged, value, show);
    h->addWidget(s, 1);
    h->addWidget(value);
    return row;
}

QSlider *SettingsDialog::slider(const char *key, int min, int max, std::function<void()> apply)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(m_ctx->settings()->integer(key));
    s->setPageStep(qMax(1, (max - min) / 10));
    connect(s, &QSlider::valueChanged, this, [this, key, apply](int v) {
        m_ctx->settings()->setValue(key, v);
        if (apply)
            apply();
    });
    return s;
}

QSpinBox *SettingsDialog::spin(const char *key, int min, int max, const QString &suffix, std::function<void()> apply)
{
    auto *s = new QSpinBox;
    s->setRange(min, max);
    s->setSuffix(suffix);
    s->setValue(m_ctx->settings()->integer(key));
    connect(s, &QSpinBox::valueChanged, this, [this, key, apply](int v) {
        m_ctx->settings()->setValue(key, v);
        if (apply)
            apply();
    });
    return s;
}

QLineEdit *SettingsDialog::line(const char *key, const QString &placeholder, std::function<void()> apply)
{
    auto *e = new QLineEdit(m_ctx->settings()->string(key));
    e->setPlaceholderText(placeholder);
    connect(e, &QLineEdit::editingFinished, this, [this, e, key, apply] {
        m_ctx->settings()->setValue(key, e->text().trimmed());
        if (apply)
            apply();
    });
    return e;
}

QComboBox *SettingsDialog::combo(const char *key, const QList<QPair<QString, QVariant>> &items,
                                 std::function<void()> apply, bool editable)
{
    auto *c = new QComboBox;
    c->setEditable(editable);
    for (const auto &item : items)
        c->addItem(item.first, item.second);
    const QVariant current = m_ctx->settings()->value(key);
    int idx = c->findData(current);
    if (idx < 0 && editable && !current.toString().isEmpty()) {
        c->addItem(current.toString(), current);
        idx = c->count() - 1;
    }
    c->setCurrentIndex(qMax(0, idx));
    auto store = [this, c, key, apply] {
        QVariant v = c->currentData();
        if (c->isEditable() && (c->currentText() != c->itemText(c->currentIndex()) || !v.isValid()))
            v = c->currentText().trimmed();
        m_ctx->settings()->setValue(key, v);
        if (apply)
            apply();
    };
    connect(c, &QComboBox::activated, this, store);
    if (editable)
        connect(c->lineEdit(), &QLineEdit::editingFinished, this, store);
    return c;
}

QWidget *SettingsDialog::secret(const QString &name, const QString &placeholder, std::function<void()> apply)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto *e = new QLineEdit(m_ctx->secrets()->get(name), w);
    e->setEchoMode(QLineEdit::Password);
    e->setPlaceholderText(placeholder);
    e->setAccessibleName(placeholder);
    auto *show = new QToolButton(w);
    show->setText(tr("Show"));
    show->setCheckable(true);
    connect(show, &QToolButton::toggled, e, [e, show](bool on) {
        e->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        show->setText(on ? QObject::tr("Hide") : QObject::tr("Show"));
    });
    connect(e, &QLineEdit::editingFinished, this, [this, e, name, apply] {
        m_ctx->secrets()->set(name, e->text());
        if (apply)
            apply();
    });
    h->addWidget(e, 1);
    h->addWidget(show);
    return w;
}

QComboBox *SettingsDialog::deviceCombo(const char *key, bool output)
{
    auto *c = new QComboBox;
    c->addItem(tr("System default"), QByteArray());
    const QList<QAudioDevice> devices = output ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    for (const QAudioDevice &d : devices) {
        QString label = d.description();
        if (output && VirtualAudio::looksLikeVirtualCable(label))
            label += tr("  (virtual cable)");
        c->addItem(label, d.id());
    }
    const QByteArray current = m_ctx->settings()->value(key).toByteArray();
    const int idx = c->findData(current);
    c->setCurrentIndex(qMax(0, idx));
    if (idx < 0 && !current.isEmpty())
        c->setToolTip(tr("The previously chosen device is not connected."));
    connect(c, &QComboBox::activated, this, [this, c, key] {
        m_ctx->settings()->setValue(key, c->currentData().toByteArray());
        m_ctx->applyAudioRouting();
    });
    return c;
}

// --- pages -------------------------------------------------------------------

QWidget *SettingsDialog::buildGeneral()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout;

    auto *theme = combo(Keys::Theme,
                        {{tr("Dark"), QStringLiteral("dark")},
                         {tr("Light"), QStringLiteral("light")},
                         {tr("High contrast"), QStringLiteral("contrast")}},
                        [this] { emit themeChanged(); });
    form->addRow(tr("Theme:"), theme);
    auto *scale = spin(Keys::FontScale, 80, 200, QStringLiteral(" %"), [this] { emit themeChanged(); });
    scale->setSingleStep(10);
    form->addRow(tr("Text size:"), scale);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(check(tr("Keep running in the system tray when the window is closed"), Keys::MinimizeToTray, {}));
    layout->addWidget(check(tr("Keep the window on top of other windows"), Keys::AlwaysOnTop, [this] { emit themeChanged(); }));
    layout->addWidget(check(tr("Clear the message box after speaking"), Keys::ClearAfterSpeak, {}));
    layout->addWidget(check(tr("Start speaking after the first sentence (faster for long messages)"), Keys::SplitSentences,
                            [this] { m_ctx->applySpeechOptions(); }));
    auto *wizard = new QPushButton(tr("Run the setup assistant again…"));
    connect(wizard, &QPushButton::clicked, this, [this] {
        emit setupWizardRequested();
        accept();
    });
    layout->addWidget(wizard, 0, Qt::AlignLeft);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildAudio()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *voiceBox = new QGroupBox(tr("Where your voice goes"));
    auto *voiceForm = new QFormLayout(voiceBox);
    voiceForm->addRow(tr("Voice output:"), deviceCombo(Keys::OutputDevice, true));
    voiceForm->addRow(tr("Volume:"), sliderRow(Keys::OutputVolume, 0, 100, QStringLiteral(" %"), tr("Voice output volume"),
                                               [this] { m_ctx->applyAudioRouting(); }));
    voiceForm->addRow(hint(tr("Pick your virtual cable here (e.g. <i>CABLE Input</i> or <i>BlackHole 2ch</i>) so "
                              "Discord, games and OBS can use Vocal Ink as a microphone."),
                           voiceBox));
    layout->addWidget(voiceBox);

    auto *monitorBox = new QGroupBox(tr("Hear yourself"));
    auto *monitorForm = new QFormLayout(monitorBox);
    monitorForm->addRow(check(tr("Also play on my speakers/headphones"), Keys::MonitorEnabled,
                              [this] { m_ctx->applyAudioRouting(); }));
    monitorForm->addRow(tr("Speakers/headphones:"), deviceCombo(Keys::MonitorDevice, true));
    monitorForm->addRow(tr("Volume:"), sliderRow(Keys::MonitorVolume, 0, 100, QStringLiteral(" %"), tr("Monitor volume"),
                                                 [this] { m_ctx->applyAudioRouting(); }));
    layout->addWidget(monitorBox);

    auto *micBox = new QGroupBox(tr("Microphone (for speech recognition)"));
    auto *micForm = new QFormLayout(micBox);
    micForm->addRow(tr("Microphone:"), deviceCombo(Keys::InputDevice, false));
    layout->addWidget(micBox);

    auto *test = new QPushButton(tr("▶ Test voice output"));
    connect(test, &QPushButton::clicked, this, [this] {
        m_ctx->speak(tr("This is a test of Vocal Ink. If you can hear me, everything works."));
    });
    layout->addWidget(test, 0, Qt::AlignLeft);

    auto *cableBox = new QGroupBox(tr("Virtual audio cable"));
    auto *cableLayout = new QVBoxLayout(cableBox);
    cableLayout->addWidget(hint(VirtualAudio::setupInstructions(), cableBox));
    if (VirtualAudio::canCreateVirtualMic()) {
        auto *row = new QHBoxLayout;
        auto *create = new QPushButton(tr("Create virtual microphone"));
        auto *remove = new QPushButton(tr("Remove it"));
        row->addWidget(create);
        row->addWidget(remove);
        row->addStretch(1);
        cableLayout->addLayout(row);
        connect(create, &QPushButton::clicked, this, [this] {
            QString error;
            if (!VirtualAudio::createVirtualMic(&error)) {
                QMessageBox::warning(this, tr("Virtual microphone"), error);
                return;
            }
            const QByteArray id = VirtualAudio::detectVirtualCableOutput();
            if (!id.isEmpty()) {
                m_ctx->settings()->setValue(Keys::OutputDevice, id);
                m_ctx->applyAudioRouting();
            }
            QMessageBox::information(this, tr("Virtual microphone"),
                                     tr("Created. Choose “Vocal Ink Microphone” as the microphone in other apps. "
                                        "Reopen Settings to see the new device in the list."));
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            QString error;
            if (!VirtualAudio::removeVirtualMic(&error))
                QMessageBox::warning(this, tr("Virtual microphone"), error);
        });
    }
    layout->addWidget(cableBox);
    layout->addStretch(1);
    return scrollable(page);
}

QWidget *SettingsDialog::buildVoices()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto applyTts = [this] { m_ctx->applySpeechOptions(); };
    auto refreshEngine = [this](const QString &id) {
        return [this, id] {
            if (TtsEngine *e = m_ctx->tts()->engine(id)) {
                emit e->availabilityChanged();
                if (e->isAvailable())
                    e->refreshVoices();
            }
        };
    };

    auto *delivery = new QGroupBox(tr("How you sound"));
    auto *deliveryForm = new QFormLayout(delivery);
    deliveryForm->addRow(tr("Speed:"), sliderRow(Keys::Rate, 50, 200, QStringLiteral(" %"), tr("Speaking speed"), applyTts));
    deliveryForm->addRow(tr("Pitch:"), sliderRow(Keys::Pitch, -50, 50, QString(), tr("Pitch"), applyTts));
    deliveryForm->addRow(hint(tr("Speed works with every voice. Pitch works with system, eSpeak and Azure voices."), delivery));
    layout->addWidget(delivery);

    auto *piper = new QGroupBox(tr("Piper — free natural voices that run on this computer"));
    auto *piperLayout = new QVBoxLayout(piper);
    auto *piperList = new ModelListWidget(ModelListWidget::Kind::PiperVoices, m_ctx, piper);
    piperList->setMinimumHeight(260);
    piperLayout->addWidget(piperList);
    auto *piperPathRow = new QHBoxLayout;
    auto *piperPath = line(Keys::PiperExecutable, tr("Advanced: use your own piper executable"), refreshEngine(QStringLiteral("piper")));
    auto *browse = new QToolButton;
    browse->setText(tr("Browse…"));
    connect(browse, &QToolButton::clicked, this, [this, piperPath] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Piper executable"));
        if (!file.isEmpty()) {
            piperPath->setText(file);
            emit piperPath->editingFinished();
        }
    });
    piperPathRow->addWidget(piperPath, 1);
    piperPathRow->addWidget(browse);
    piperLayout->addLayout(piperPathRow);
    layout->addWidget(piper);

    auto *azure = new QGroupBox(tr("Microsoft Azure"));
    auto *azureForm = new QFormLayout(azure);
    azureForm->addRow(tr("Speech key:"), secret(Secrets::Azure, tr("Azure Speech resource key"), refreshEngine(QStringLiteral("azure"))));
    azureForm->addRow(tr("Region:"), combo(Keys::AzureRegion,
                                           {{QStringLiteral("eastus"), QStringLiteral("eastus")},
                                            {QStringLiteral("eastus2"), QStringLiteral("eastus2")},
                                            {QStringLiteral("westus"), QStringLiteral("westus")},
                                            {QStringLiteral("westus2"), QStringLiteral("westus2")},
                                            {QStringLiteral("centralus"), QStringLiteral("centralus")},
                                            {QStringLiteral("canadacentral"), QStringLiteral("canadacentral")},
                                            {QStringLiteral("westeurope"), QStringLiteral("westeurope")},
                                            {QStringLiteral("northeurope"), QStringLiteral("northeurope")},
                                            {QStringLiteral("uksouth"), QStringLiteral("uksouth")},
                                            {QStringLiteral("francecentral"), QStringLiteral("francecentral")},
                                            {QStringLiteral("germanywestcentral"), QStringLiteral("germanywestcentral")},
                                            {QStringLiteral("southeastasia"), QStringLiteral("southeastasia")},
                                            {QStringLiteral("japaneast"), QStringLiteral("japaneast")},
                                            {QStringLiteral("australiaeast"), QStringLiteral("australiaeast")},
                                            {QStringLiteral("centralindia"), QStringLiteral("centralindia")}},
                                           refreshEngine(QStringLiteral("azure")), true));
    azureForm->addRow(hint(tr("Hundreds of neural voices in 140+ languages. Create a free Speech resource at "
                              "<a href=\"https://portal.azure.com/#create/Microsoft.CognitiveServicesSpeechServices\">portal.azure.com</a>."),
                           azure));
    layout->addWidget(azure);

    auto *eleven = new QGroupBox(tr("ElevenLabs"));
    auto *elevenForm = new QFormLayout(eleven);
    elevenForm->addRow(tr("API key:"), secret(Secrets::ElevenLabs, tr("ElevenLabs API key"), refreshEngine(QStringLiteral("elevenlabs"))));
    elevenForm->addRow(tr("Model:"), combo(Keys::ElevenLabsModel,
                                           {{tr("Flash v2.5 — fastest (recommended)"), QStringLiteral("eleven_flash_v2_5")},
                                            {tr("Multilingual v2 — richest"), QStringLiteral("eleven_multilingual_v2")},
                                            {tr("v3 — most expressive"), QStringLiteral("eleven_v3")}},
                                           {}, true));
    elevenForm->addRow(hint(tr("Very natural voices and your own cloned voice. Get a key at "
                               "<a href=\"https://elevenlabs.io/app/settings/api-keys\">elevenlabs.io</a>."),
                            eleven));
    layout->addWidget(eleven);

    auto *fish = new QGroupBox(tr("Fish Audio"));
    auto *fishForm = new QFormLayout(fish);
    fishForm->addRow(tr("API key:"), secret(Secrets::FishAudio, tr("Fish Audio API key"), refreshEngine(QStringLiteral("fishaudio"))));
    fishForm->addRow(tr("Model:"), combo(Keys::FishModel,
                                         {{QStringLiteral("s1"), QStringLiteral("s1")},
                                          {QStringLiteral("s2-pro"), QStringLiteral("s2-pro")},
                                          {QStringLiteral("s2.1-pro"), QStringLiteral("s2.1-pro")},
                                          {QStringLiteral("s2.1-pro-free"), QStringLiteral("s2.1-pro-free")}},
                                         {}, true));
    fishForm->addRow(hint(tr("A huge library of community voices (search them in the voice browser) and your own clones. "
                             "Get a key at <a href=\"https://fish.audio/app/api-keys/\">fish.audio</a>."),
                          fish));
    layout->addWidget(fish);

    auto *openai = new QGroupBox(tr("OpenAI (or any OpenAI-compatible server)"));
    auto *openaiForm = new QFormLayout(openai);
    openaiForm->addRow(tr("API key:"), secret(Secrets::OpenAi, tr("OpenAI API key"), refreshEngine(QStringLiteral("openai"))));
    openaiForm->addRow(tr("Model:"), combo(Keys::OpenAiTtsModel,
                                           {{QStringLiteral("gpt-4o-mini-tts"), QStringLiteral("gpt-4o-mini-tts")},
                                            {QStringLiteral("tts-1"), QStringLiteral("tts-1")},
                                            {QStringLiteral("tts-1-hd"), QStringLiteral("tts-1-hd")}},
                                           {}, true));
    openaiForm->addRow(tr("Server:"), line(Keys::OpenAiBaseUrl, QStringLiteral("https://api.openai.com/v1"), refreshEngine(QStringLiteral("openai"))));
    openaiForm->addRow(tr("Delivery style:"), line(Keys::OpenAiInstructions, tr("e.g. “Warm and upbeat, speak quickly”"), applyTts));
    layout->addWidget(openai);

    layout->addWidget(hint(tr("Keys are stored in your system's password manager, never in plain settings files "
                              "(unless no password manager is available)."),
                           page));
    layout->addStretch(1);
    return scrollable(page);
}

QWidget *SettingsDialog::buildStt()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto apply = [this] { m_ctx->applySttSettings(); };

    auto *how = new QGroupBox(tr("How speaking works"));
    auto *howForm = new QFormLayout(how);
    QList<QPair<QString, QVariant>> engines;
    const QStringList ids = availableSttEngineIds();
    if (ids.contains(QStringLiteral("whisper")))
        engines.append({tr("Whisper — on this computer (private, free)"), QStringLiteral("whisper")});
    if (ids.contains(QStringLiteral("openai")))
        engines.append({tr("OpenAI-compatible cloud service"), QStringLiteral("openai")});
    howForm->addRow(tr("Recognizer:"), combo(Keys::SttEngine, engines, apply));
    howForm->addRow(tr("Microphone mode:"),
                    combo(Keys::SttMode,
                          {{tr("Push-to-talk — hold the button or shortcut"), QStringLiteral("ptt")},
                           {tr("Toggle — press once to start, again to stop"), QStringLiteral("toggle")},
                           {tr("Hands-free — listens and detects when you talk"), QStringLiteral("vad")}},
                          apply));
    howForm->addRow(tr("Hands-free sensitivity:"),
                    sliderRow(Keys::SttVadSensitivity, 0, 100, QStringLiteral(" %"), tr("Hands-free sensitivity"), apply));
    howForm->addRow(hint(tr("Raise the sensitivity if you speak very quietly or whisper; lower it in noisy rooms."), how));
    howForm->addRow(check(tr("Speak what I said right away (skip reviewing the text first)"), Keys::SttAutoSpeak, {}));
    howForm->addRow(tr("Language:"), combo(Keys::SttLanguage, languages(), apply, true));
    auto *prompt = line(Keys::SttPrompt, tr("Names and words you use a lot, e.g. “Vocal Ink, Minecraft, Aisha”"), apply);
    howForm->addRow(tr("Vocabulary hints:"), prompt);
    layout->addWidget(how);

    auto *models = new QGroupBox(tr("Whisper models (local)"));
    auto *modelsLayout = new QVBoxLayout(models);
    modelsLayout->addWidget(hint(tr("Bigger models understand unclear or accented speech better but need a faster "
                                    "computer. <b>Base (English)</b> is a good start; choose a multilingual model for "
                                    "other languages."),
                                 models));
    auto *list = new ModelListWidget(ModelListWidget::Kind::Whisper, m_ctx, models);
    list->setMinimumHeight(220);
    modelsLayout->addWidget(list);
    layout->addWidget(models);

    auto *cloud = new QGroupBox(tr("Cloud recognizer"));
    auto *cloudForm = new QFormLayout(cloud);
    cloudForm->addRow(tr("API key:"), secret(Secrets::OpenAiStt, tr("Leave empty to use the OpenAI key from Voices"), apply));
    cloudForm->addRow(tr("Model:"), combo(Keys::SttOpenAiModel,
                                          {{QStringLiteral("gpt-4o-mini-transcribe"), QStringLiteral("gpt-4o-mini-transcribe")},
                                           {QStringLiteral("gpt-4o-transcribe"), QStringLiteral("gpt-4o-transcribe")},
                                           {QStringLiteral("whisper-1"), QStringLiteral("whisper-1")}},
                                          apply, true));
    cloudForm->addRow(tr("Server:"), line(Keys::SttOpenAiBaseUrl, QStringLiteral("https://api.openai.com/v1"), apply));
    cloudForm->addRow(hint(tr("Works with OpenAI, Groq and self-hosted servers such as Speaches that offer "
                              "<code>/audio/transcriptions</code>."),
                           cloud));
    layout->addWidget(cloud);
    layout->addStretch(1);
    return scrollable(page);
}

QWidget *SettingsDialog::buildObs()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto applyObs = [this] {
        m_ctx->applyObsSettings();
        refreshObsStatus();
    };

    // --- Caption overlay (works without any OBS setup) ---
    auto *overlay = new QGroupBox(tr("Caption overlay (Browser Source)"));
    auto *overlayForm = new QFormLayout(overlay);
    overlayForm->addRow(check(tr("Serve live captions for OBS, Streamlabs or any browser source"), Keys::OverlayEnabled,
                              [this] {
                                  m_ctx->applyOverlaySettings();
                                  updateOverlayUrl();
                              }));
    const QUrlQuery q(m_ctx->settings()->string(Keys::OverlayQuery));
    m_overlayStyle = new QComboBox;
    m_overlayStyle->addItem(tr("Subtitles (bottom bar)"), QStringLiteral("subtitles"));
    m_overlayStyle->addItem(tr("Speech bubble"), QStringLiteral("bubble"));
    m_overlayStyle->addItem(tr("Plain outlined text"), QStringLiteral("plain"));
    m_overlayStyle->setCurrentIndex(qMax(0, m_overlayStyle->findData(q.queryItemValue(QStringLiteral("style")))));
    m_overlaySize = new QSpinBox;
    m_overlaySize->setRange(16, 160);
    m_overlaySize->setSuffix(QStringLiteral(" px"));
    m_overlaySize->setValue(q.hasQueryItem(QStringLiteral("size")) ? q.queryItemValue(QStringLiteral("size")).toInt() : 42);
    m_overlayReveal = new QComboBox;
    m_overlayReveal->addItem(tr("Word by word"), QStringLiteral("word"));
    m_overlayReveal->addItem(tr("All at once"), QStringLiteral("instant"));
    m_overlayReveal->setCurrentIndex(qMax(0, m_overlayReveal->findData(q.queryItemValue(QStringLiteral("reveal")))));
    m_overlayName = new QCheckBox(tr("Show the voice name"));
    m_overlayName->setChecked(q.queryItemValue(QStringLiteral("name")) == QLatin1String("1"));
    overlayForm->addRow(tr("Style:"), m_overlayStyle);
    overlayForm->addRow(tr("Text size:"), m_overlaySize);
    overlayForm->addRow(tr("Reveal:"), m_overlayReveal);
    overlayForm->addRow(m_overlayName);
    overlayForm->addRow(tr("Port:"), spin(Keys::OverlayPort, 1024, 65535, QString(), [this] {
        m_ctx->applyOverlaySettings();
        updateOverlayUrl();
    }));
    overlayForm->addRow(check(tr("Allow other computers on my network (two-PC streaming)"), Keys::OverlayAllowLan,
                              [this] { m_ctx->applyOverlaySettings(); }));
    auto saveQuery = [this] {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("style"), m_overlayStyle->currentData().toString());
        query.addQueryItem(QStringLiteral("size"), QString::number(m_overlaySize->value()));
        query.addQueryItem(QStringLiteral("reveal"), m_overlayReveal->currentData().toString());
        if (m_overlayName->isChecked())
            query.addQueryItem(QStringLiteral("name"), QStringLiteral("1"));
        m_ctx->settings()->setValue(Keys::OverlayQuery, query.toString());
        updateOverlayUrl();
    };
    connect(m_overlayStyle, &QComboBox::activated, this, saveQuery);
    connect(m_overlaySize, &QSpinBox::valueChanged, this, saveQuery);
    connect(m_overlayReveal, &QComboBox::activated, this, saveQuery);
    connect(m_overlayName, &QCheckBox::toggled, this, saveQuery);

    m_overlayUrl = new QLineEdit;
    m_overlayUrl->setReadOnly(true);
    m_overlayUrl->setAccessibleName(tr("Overlay URL"));
    auto *copy = new QPushButton(tr("Copy"));
    auto *open = new QPushButton(tr("Open in browser"));
    auto *add = new QPushButton(tr("Add to current OBS scene"));
    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(m_overlayUrl, 1);
    urlRow->addWidget(copy);
    urlRow->addWidget(open);
    overlayForm->addRow(tr("URL:"), urlRow);
    overlayForm->addRow(add);
    overlayForm->addRow(hint(tr("In OBS: Sources → + → Browser, paste the URL, width 1920, height 1080. "
                                "Or click <i>Add to current OBS scene</i> once connected below."),
                             overlay));
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_overlayUrl->text()); });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_overlayUrl->text())); });
    connect(add, &QPushButton::clicked, this, [this] {
        QPointer<SettingsDialog> self(this);
        m_ctx->obs()->addBrowserOverlay(tr("Vocal Ink captions"), QUrl(m_overlayUrl->text()),
                                        [self](bool ok, const QString &msg) {
                                            if (!self)
                                                return;
                                            if (ok)
                                                QMessageBox::information(self, tr("OBS"), tr("Added the caption overlay to your current scene."));
                                            else
                                                QMessageBox::warning(self, tr("OBS"), msg);
                                        });
    });
    layout->addWidget(overlay);

    // --- obs-websocket connection ---
    auto *conn = new QGroupBox(tr("Connect to OBS (WebSocket)"));
    auto *connForm = new QFormLayout(conn);
    connForm->addRow(check(tr("Connect to OBS Studio"), Keys::ObsEnabled, applyObs));
    connForm->addRow(tr("Host:"), line(Keys::ObsHost, QStringLiteral("127.0.0.1"), applyObs));
    connForm->addRow(tr("Port:"), spin(Keys::ObsPort, 1, 65535, QString(), applyObs));
    connForm->addRow(tr("Password:"), secret(Secrets::Obs, tr("OBS WebSocket password"), applyObs));
    m_obsStatus = new QLabel;
    m_obsStatus->setWordWrap(true);
    auto *reconnect = new QPushButton(tr("Reconnect"));
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_obsStatus, 1);
    statusRow->addWidget(reconnect);
    connForm->addRow(tr("Status:"), statusRow);
    connForm->addRow(hint(tr("In OBS open <b>Tools → WebSocket Server Settings</b>, tick <i>Enable WebSocket "
                             "server</i> and use <i>Show Connect Info</i> to copy the password."),
                          conn));
    connect(reconnect, &QPushButton::clicked, this, [this] { m_ctx->obs()->reconnect(); });
    connect(m_ctx->obs(), &ObsIntegration::statusChanged, this, [this](ObsIntegration::Status status, const QString &) {
        refreshObsStatus();
        if (status == ObsIntegration::Status::Connected)
            refreshObsSources();
    });
    layout->addWidget(conn);

    // --- Features that use the connection ---
    auto *features = new QGroupBox(tr("Show what you say on stream"));
    auto *featForm = new QFormLayout(features);
    featForm->addRow(check(tr("Subtitles in a Text source"), Keys::ObsSubtitlesEnabled, applyObs));
    m_subtitleSource = new QComboBox;
    m_subtitleSource->setEditable(true);
    m_subtitleSource->setCurrentText(m_ctx->settings()->string(Keys::ObsSubtitlesSource));
    auto storeSubtitle = [this] {
        m_ctx->settings()->setValue(Keys::ObsSubtitlesSource, m_subtitleSource->currentText().trimmed());
        m_ctx->applyObsSettings();
    };
    connect(m_subtitleSource, &QComboBox::activated, this, storeSubtitle);
    connect(m_subtitleSource->lineEdit(), &QLineEdit::editingFinished, this, storeSubtitle);
    auto *create = new QPushButton(tr("Create one"));
    connect(create, &QPushButton::clicked, this, [this] {
        QPointer<SettingsDialog> self(this);
        const QString name = tr("Vocal Ink subtitles");
        m_ctx->obs()->createTextSource(name, [self, name](bool ok, const QString &msg) {
            if (!self)
                return;
            if (!ok) {
                QMessageBox::warning(self, tr("OBS"), msg);
                return;
            }
            self->m_subtitleSource->setCurrentText(name);
            self->m_ctx->settings()->setValue(Keys::ObsSubtitlesSource, name);
            self->m_ctx->settings()->setValue(Keys::ObsSubtitlesEnabled, true);
            self->m_ctx->applyObsSettings();
        });
    });
    auto *test = new QPushButton(tr("Test"));
    connect(test, &QPushButton::clicked, this, [this] {
        QPointer<SettingsDialog> self(this);
        m_ctx->obs()->testSubtitle([self](bool ok, const QString &msg) {
            if (self && !ok)
                QMessageBox::warning(self, tr("OBS"), msg);
        });
    });
    auto *subRow = new QHBoxLayout;
    subRow->addWidget(m_subtitleSource, 1);
    subRow->addWidget(create);
    subRow->addWidget(test);
    featForm->addRow(tr("Text source:"), subRow);
    auto *clear = spin(Keys::ObsSubtitlesClearMs, 0, 60000, tr(" ms"), applyObs);
    clear->setSingleStep(500);
    clear->setSpecialValueText(tr("Never"));
    featForm->addRow(tr("Clear after:"), clear);
    featForm->addRow(check(tr("Closed captions (CC) for Twitch/YouTube while streaming"), Keys::ObsCaptionsEnabled, applyObs));
    featForm->addRow(check(tr("Show a source while my voice is playing (e.g. a talking avatar)"), Keys::ObsIndicatorEnabled, applyObs));
    m_indicatorSource = new QComboBox;
    m_indicatorSource->setEditable(true);
    m_indicatorSource->setCurrentText(m_ctx->settings()->string(Keys::ObsIndicatorSource));
    auto storeIndicator = [this] {
        m_ctx->settings()->setValue(Keys::ObsIndicatorSource, m_indicatorSource->currentText().trimmed());
        m_ctx->applyObsSettings();
    };
    connect(m_indicatorSource, &QComboBox::activated, this, storeIndicator);
    connect(m_indicatorSource->lineEdit(), &QLineEdit::editingFinished, this, storeIndicator);
    featForm->addRow(tr("Source to show:"), m_indicatorSource);
    auto *refresh = new QPushButton(tr("Refresh source lists"));
    connect(refresh, &QPushButton::clicked, this, &SettingsDialog::refreshObsSources);
    featForm->addRow(refresh);
    layout->addWidget(features);
    layout->addStretch(1);

    refreshObsStatus();
    updateOverlayUrl();
    if (m_ctx->obs()->status() == ObsIntegration::Status::Connected)
        refreshObsSources();
    return scrollable(page);
}

void SettingsDialog::refreshObsStatus()
{
    if (!m_obsStatus)
        return;
    const ObsIntegration::Status s = m_ctx->obs()->status();
    QString text = m_ctx->obs()->statusText();
    if (text.isEmpty())
        text = s == ObsIntegration::Status::Disabled ? tr("Not connected") : tr("Connecting…");
    const char *chip = s == ObsIntegration::Status::Connected ? "ok"
        : (s == ObsIntegration::Status::Error || s == ObsIntegration::Status::AuthFailed) ? "bad"
                                                                                           : "true";
    m_obsStatus->setText(text);
    m_obsStatus->setProperty("chip", QString::fromLatin1(chip));
    m_obsStatus->style()->unpolish(m_obsStatus);
    m_obsStatus->style()->polish(m_obsStatus);
}

void SettingsDialog::refreshObsSources()
{
    QPointer<SettingsDialog> self(this);
    m_ctx->obs()->listTextSources([self](const QStringList &names, const QString &error) {
        if (!self || !error.isEmpty())
            return;
        const QString current = self->m_subtitleSource->currentText();
        self->m_subtitleSource->clear();
        self->m_subtitleSource->addItems(names);
        self->m_subtitleSource->setCurrentText(current);
    });
    m_ctx->obs()->listAllSources([self](const QStringList &names, const QString &error) {
        if (!self || !error.isEmpty())
            return;
        const QString current = self->m_indicatorSource->currentText();
        self->m_indicatorSource->clear();
        self->m_indicatorSource->addItems(names);
        self->m_indicatorSource->setCurrentText(current);
    });
}

void SettingsDialog::updateOverlayUrl()
{
    if (!m_overlayUrl)
        return;
    const bool running = m_ctx->overlay()->isRunning();
    m_overlayUrl->setText(running ? m_ctx->overlayUrl() : tr("(overlay is off)"));
    m_overlayUrl->setEnabled(running);
}

QWidget *SettingsDialog::buildHotkeys()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    if (!GlobalHotkeys::isSupported())
        layout->addWidget(hint(QStringLiteral("<b>%1</b>").arg(GlobalHotkeys::unsupportedReason().toHtmlEscaped()), page));
    layout->addWidget(hint(tr("These shortcuts work everywhere, even while a game or another app is focused. "
                              "Quick phrases can have their own shortcut: right-click a phrase → Edit."),
                           page));
    auto *form = new QFormLayout;
    auto addKey = [this, form](const QString &label, const char *key) {
        auto *edit = new QKeySequenceEdit(QKeySequence::fromString(m_ctx->settings()->string(key), QKeySequence::PortableText));
        edit->setAccessibleName(label);
        auto *clear = new QToolButton;
        clear->setText(tr("Clear"));
        auto *row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(clear);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, edit, key] {
            // Keep only the first chord; global shortcuts are single combinations.
            const QKeySequence seq = edit->keySequence().isEmpty() ? QKeySequence() : QKeySequence(edit->keySequence()[0]);
            edit->setKeySequence(seq);
            m_ctx->settings()->setValue(key, seq.toString(QKeySequence::PortableText));
            m_ctx->applyHotkeys();
        });
        connect(clear, &QToolButton::clicked, this, [this, edit, key] {
            edit->clear();
            m_ctx->settings()->setValue(key, QString());
            m_ctx->applyHotkeys();
        });
        form->addRow(label, row);
    };
    addKey(tr("Push-to-talk (hold):"), Keys::HotkeyPushToTalk);
    addKey(tr("Stop speaking:"), Keys::HotkeyStop);
    addKey(tr("Quick-type box:"), Keys::HotkeyQuickType);
    addKey(tr("Repeat last message:"), Keys::HotkeyRepeat);
    layout->addLayout(form);
    layout->addWidget(hint(tr("In the main window: <b>Enter</b> speaks, <b>Esc</b> stops, <b>Up</b> recalls earlier "
                              "messages, <b>Tab</b> completes words, <b>Alt+1…9</b> say quick phrases."),
                           page));
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildText()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(hint(tr("Shortcuts you type that get spoken as something longer — or words the voice "
                              "mispronounces, spelled the way they sound."),
                           page));
    m_replacements = new QTableWidget(0, 2, page);
    m_replacements->setHorizontalHeaderLabels({tr("When I type"), tr("Say")});
    m_replacements->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_replacements->horizontalHeader()->setStretchLastSection(true);
    m_replacements->verticalHeader()->hide();
    m_replacements->setAccessibleName(tr("Text replacements"));
    const QVariantMap map = m_ctx->settings()->value(Keys::Replacements).toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const int row = m_replacements->rowCount();
        m_replacements->insertRow(row);
        m_replacements->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_replacements->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
    connect(m_replacements, &QTableWidget::itemChanged, this, &SettingsDialog::saveReplacements);
    layout->addWidget(m_replacements, 1);

    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add"));
    auto *remove = new QPushButton(tr("Remove selected"));
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this] {
        const int r = m_replacements->rowCount();
        m_replacements->insertRow(r);
        m_replacements->setItem(r, 0, new QTableWidgetItem);
        m_replacements->setItem(r, 1, new QTableWidgetItem);
        m_replacements->editItem(m_replacements->item(r, 0));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto rows = m_replacements->selectionModel()->selectedRows();
        QList<int> indices;
        for (const QModelIndex &i : rows)
            indices << i.row();
        if (indices.isEmpty() && m_replacements->currentRow() >= 0)
            indices << m_replacements->currentRow();
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int r : std::as_const(indices))
            m_replacements->removeRow(r);
        saveReplacements();
    });
    return page;
}

void SettingsDialog::saveReplacements()
{
    QVariantMap map;
    for (int r = 0; r < m_replacements->rowCount(); ++r) {
        const QTableWidgetItem *from = m_replacements->item(r, 0);
        const QTableWidgetItem *to = m_replacements->item(r, 1);
        if (from && to && !from->text().trimmed().isEmpty())
            map.insert(from->text().trimmed(), to->text().trimmed());
    }
    m_ctx->settings()->setValue(Keys::Replacements, map);
}
