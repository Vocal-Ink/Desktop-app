#include "ui/MainWindow.h"

#include "Version.h"
#include "app/AppContext.h"
#include "audio/AudioPlayer.h"
#include "core/HistoryModel.h"
#include "core/PhraseStore.h"
#include "core/Settings.h"
#include "core/SpeechQueue.h"
#include "core/TextProcessor.h"
#include "obs/ObsIntegration.h"
#include "obs/OverlayServer.h"
#include "stt/SttController.h"
#include "tts/TtsRegistry.h"
#include "ui/Icons.h"
#include "ui/LevelMeter.h"
#include "ui/MessageEdit.h"
#include "ui/PhraseGrid.h"
#include "ui/QuickTypePopup.h"
#include "ui/SettingsDialog.h"
#include "ui/SetupWizard.h"
#include "ui/Theme.h"
#include "ui/VoicePickerDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const QString kBrowseVoices = QStringLiteral("__browse__");

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}
} // namespace

MainWindow::MainWindow(AppContext *context, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(context)
{
    setWindowTitle(QStringLiteral(VOCALINK_DISPLAY_NAME));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    setMinimumSize(640, 480);
    resize(1100, 720);

    applyTheme();
    buildUi();
    buildMenus();
    buildTray();
    wire();

    m_quickType = new QuickTypePopup(m_ctx, nullptr);
    m_quickType->setWindowIcon(windowIcon());

    const QByteArray geometry = m_ctx->settings()->value(Keys::WindowGeometry, QByteArray()).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    refreshVoiceCombo();
    updateMicButton();
    updateStatus();
    m_edit->setFocus();
}

void MainWindow::applyTheme()
{
    Theme::apply(qApp, m_ctx->settings()->string(Keys::Theme), m_ctx->settings()->integer(Keys::FontScale));
    const bool onTop = m_ctx->settings()->flag(Keys::AlwaysOnTop);
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) != onTop) {
        const bool visible = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
        if (visible)
            show();
    }
}

void MainWindow::buildUi()
{
    // --- Toolbar: voice, speed, status -----------------------------------------
    auto *bar = addToolBar(tr("Voice"));
    bar->setObjectName(QStringLiteral("voiceToolbar"));
    bar->setMovable(false);
    bar->setFloatable(false);

    bar->addWidget(new QLabel(tr("Voice:"), bar));
    m_voiceCombo = new QComboBox(bar);
    m_voiceCombo->setMinimumContentsLength(22);
    m_voiceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_voiceCombo->setAccessibleName(tr("Voice"));
    m_voiceCombo->setToolTip(tr("Your favorite voices. Choose “More voices…” to browse every provider."));
    bar->addWidget(m_voiceCombo);
    auto *browse = new QToolButton(bar);
    browse->setText(tr("Browse voices…"));
    connect(browse, &QToolButton::clicked, this, &MainWindow::openVoicePicker);
    bar->addWidget(browse);

    bar->addSeparator();
    bar->addWidget(new QLabel(tr("Speed:"), bar));
    m_speed = new QSlider(Qt::Horizontal, bar);
    m_speed->setRange(50, 200);
    m_speed->setValue(m_ctx->settings()->integer(Keys::Rate));
    m_speed->setFixedWidth(130);
    m_speed->setAccessibleName(tr("Speaking speed"));
    m_speed->setToolTip(tr("Speaking speed: %1%").arg(m_speed->value()));
    bar->addWidget(m_speed);

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    auto chip = [bar](const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setAutoRaise(true);
        b->setProperty("chip", true);
        b->setToolTip(tip);
        bar->addWidget(b);
        return b;
    };
    m_sttChip = chip(tr("Speech recognition status — click to configure"));
    m_overlayChip = chip(tr("Stream caption overlay — click to configure"));
    m_obsChip = chip(tr("OBS connection — click to configure"));
    auto *settings = new QToolButton(bar);
    settings->setText(tr("⚙ Settings"));
    settings->setAccessibleName(tr("Settings"));
    connect(settings, &QToolButton::clicked, this, [this] { showSettings(); });
    bar->addWidget(settings);

    // --- Left: phrases + message ------------------------------------------------
    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 8, 6, 12);
    leftLayout->setSpacing(10);

    m_banner = new QLabel(left);
    m_banner->setWordWrap(true);
    m_banner->setTextFormat(Qt::RichText);
    m_banner->setObjectName(QStringLiteral("card"));
    m_banner->setContentsMargins(12, 8, 12, 8);
    m_banner->hide();
    m_bannerTimer = new QTimer(this);
    m_bannerTimer->setSingleShot(true);
    connect(m_bannerTimer, &QTimer::timeout, m_banner, &QLabel::hide);
    connect(m_banner, &QLabel::linkActivated, this, [this](const QString &link) {
        m_banner->hide();
        if (link == QLatin1String("voices-picker"))
            openVoicePicker();
        else
            showSettings(link);
    });
    leftLayout->addWidget(m_banner);

    leftLayout->addWidget(sectionLabel(tr("Quick phrases"), left));
    m_phrases = new PhraseGrid(m_ctx->phrases(), m_ctx->tts(), left);
    auto *phraseScroll = new QScrollArea(left);
    phraseScroll->setWidgetResizable(true);
    phraseScroll->setWidget(m_phrases);
    phraseScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    phraseScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    phraseScroll->viewport()->installEventFilter(this);
    m_phraseScroll = phraseScroll;
    connect(m_ctx->phrases(), &PhraseStore::changed, this, &MainWindow::fitPhrases, Qt::QueuedConnection);
    leftLayout->addWidget(phraseScroll);

    leftLayout->addWidget(sectionLabel(tr("Say something"), left));
    m_edit = new MessageEdit(left);
    m_edit->setRecallHistory(m_ctx->history()->texts());
    for (const Phrase &p : m_ctx->phrases()->phrases())
        m_edit->addVocabulary(TextProcessor::vocabularyFrom(p.text));
    leftLayout->addWidget(m_edit, 1);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    m_micButton = new QPushButton(left);
    m_micButton->setObjectName(QStringLiteral("micButton"));
    m_micButton->setAccessibleName(tr("Talk"));
    auto *micColumn = new QVBoxLayout;
    micColumn->setSpacing(4);
    m_level = new LevelMeter(left);
    m_micStatus = new QLabel(left);
    m_micStatus->setProperty("hint", true);
    micColumn->addWidget(m_level);
    micColumn->addWidget(m_micStatus);
    m_queueLabel = new QLabel(left);
    m_queueLabel->setProperty("hint", true);
    m_stopButton = new QPushButton(tr("■ Stop"), left);
    m_stopButton->setToolTip(tr("Stop speaking and clear the queue (Esc)"));
    m_stopButton->setEnabled(false);
    m_speakButton = new QPushButton(tr("Speak  ⏎"), left);
    m_speakButton->setProperty("primary", true);
    m_speakButton->setToolTip(tr("Say the message (Enter)"));
    controls->addWidget(m_micButton);
    controls->addLayout(micColumn, 1);
    controls->addWidget(m_queueLabel);
    controls->addWidget(m_stopButton);
    controls->addWidget(m_speakButton);
    leftLayout->addLayout(controls);

    // --- Right: history --------------------------------------------------------
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(6, 8, 12, 12);
    rightLayout->addWidget(sectionLabel(tr("History"), right));
    m_history = new QListView(right);
    m_history->setModel(m_ctx->history());
    m_history->setWordWrap(true);
    m_history->setUniformItemSizes(false);
    m_history->setAlternatingRowColors(true);
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->setContextMenuPolicy(Qt::CustomContextMenu);
    m_history->setAccessibleName(tr("Message history. Double-click or press Enter to say again."));
    rightLayout->addWidget(m_history, 1);
    auto *hint = new QLabel(tr("Double-click to say again • right-click for more"), right);
    hint->setProperty("hint", true);
    rightLayout->addWidget(hint);

    auto *split = new QSplitter(this);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setChildrenCollapsible(false);
    setCentralWidget(split);

    m_outputLabel = new QLabel(this);
    m_outputLabel->setProperty("hint", true);
    statusBar()->addPermanentWidget(m_outputLabel);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Setup &assistant…"), this, &MainWindow::runSetupWizard);
    QAction *prefs = file->addAction(tr("&Settings…"), this, [this] { showSettings(); });
    prefs->setShortcut(QKeySequence::Preferences);
    prefs->setMenuRole(QAction::PreferencesRole);
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    QMenu *speech = menuBar()->addMenu(tr("&Speech"));
    QAction *speak = speech->addAction(tr("&Speak message"), this, [this] { speakMessage(m_edit->toPlainText()); });
    speak->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    QAction *stop = speech->addAction(tr("S&top speaking"), this, [this] { m_ctx->stopSpeaking(); });
    stop->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    speech->addAction(tr("S&kip current message"), this, [this] { m_ctx->skipCurrent(); });
    QAction *repeat = speech->addAction(tr("&Repeat last message"), this, [this] { m_ctx->repeatLast(); });
    repeat->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    speech->addSeparator();
    QAction *listen = speech->addAction(tr("Start/stop &listening"), this, [this] { m_ctx->stt()->toggleListening(); });
    listen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    QAction *focus = speech->addAction(tr("&Focus the message box"), this, [this] { m_edit->setFocus(); });
    focus->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    speech->addAction(tr("&Quick-type box"), this, [this] { m_quickType->popup(); });
    speech->addSeparator();
    speech->addAction(tr("Choose &voice…"), this, &MainWindow::openVoicePicker);
    speech->addAction(tr("Add &quick phrase…"), this, [this] { m_phrases->addPhrase(); });

    QMenu *stream = menuBar()->addMenu(tr("S&tream"));
    stream->addAction(tr("&Copy caption overlay URL"), this, [this] {
        if (!m_ctx->overlay()->isRunning()) {
            showSettings(QStringLiteral("obs"));
            return;
        }
        QApplication::clipboard()->setText(m_ctx->overlayUrl());
        statusBar()->showMessage(tr("Overlay URL copied. In OBS add a Browser source and paste it."), 6000);
    });
    stream->addAction(tr("&Add caption overlay to OBS"), this, [this] {
        m_ctx->obs()->addBrowserOverlay(tr("Vocal Ink captions"), QUrl(m_ctx->overlayUrl()), [this](bool ok, const QString &msg) {
            if (ok)
                statusBar()->showMessage(tr("Caption overlay added to the current OBS scene."), 6000);
            else
                showBanner(msg, 1, QStringLiteral("obs"));
        });
    });
    stream->addAction(tr("OBS &settings…"), this, [this] { showSettings(QStringLiteral("obs")); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *onTop = view->addAction(tr("Keep window on &top"));
    onTop->setCheckable(true);
    onTop->setChecked(m_ctx->settings()->flag(Keys::AlwaysOnTop));
    connect(onTop, &QAction::toggled, this, [this](bool on) {
        m_ctx->settings()->setValue(Keys::AlwaysOnTop, on);
        applyTheme();
    });
    QAction *bigger = view->addAction(tr("&Larger text"), this, [this] {
        m_ctx->settings()->setValue(Keys::FontScale, qMin(200, m_ctx->settings()->integer(Keys::FontScale) + 10));
        applyTheme();
    });
    bigger->setShortcut(QKeySequence::ZoomIn);
    QAction *smaller = view->addAction(tr("&Smaller text"), this, [this] {
        m_ctx->settings()->setValue(Keys::FontScale, qMax(80, m_ctx->settings()->integer(Keys::FontScale) - 10));
        applyTheme();
    });
    smaller->setShortcut(QKeySequence::ZoomOut);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&User guide"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(VOCALINK_HOMEPAGE "#readme")));
    });
    help->addAction(tr("&Virtual cable setup"), this, [this] { showSettings(QStringLiteral("audio")); });
    help->addAction(tr("Report a &problem"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(VOCALINK_HOMEPAGE "/issues")));
    });
    help->addSeparator();
    QAction *about = help->addAction(tr("&About Vocal Ink"), this, [this] {
        QMessageBox::about(this, tr("About Vocal Ink"),
                           tr("<h3>Vocal Ink %1</h3>"
                              "<p>Speak-for-me text-to-speech for people who can't, or don't want to, use their own voice.</p>"
                              "<p><a href=\"%2\">%2</a></p>"
                              "<p>Licensed under the Apache License 2.0. Built with Qt, whisper.cpp, QHotkey and "
                              "QtKeychain; Piper voices are downloaded on request. See THIRD_PARTY_NOTICES.md.</p>")
                               .arg(QStringLiteral(VOCALINK_VERSION), QStringLiteral(VOCALINK_HOMEPAGE)));
    });
    about->setMenuRole(QAction::AboutRole);

    // Alt+1..9 speak the first nine quick phrases.
    for (int i = 0; i < 9; ++i) {
        auto *sc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)), this);
        connect(sc, &QShortcut::activated, this, [this, i] { speakPhrase(i); });
    }
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setToolTip(QStringLiteral(VOCALINK_DISPLAY_NAME));
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show Vocal Ink"), this, &MainWindow::showAndRaise);
    menu->addAction(tr("Quick-type box"), this, [this] { m_quickType->popup(); });
    menu->addAction(tr("Stop speaking"), this, [this] { m_ctx->stopSpeaking(); });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible() && !isMinimized())
                hide();
            else
                showAndRaise();
        }
    });
    m_tray->show();
}

void MainWindow::wire()
{
    connect(m_edit, &MessageEdit::submitted, this, &MainWindow::speakMessage);
    connect(m_edit, &MessageEdit::stopRequested, this, [this] { m_ctx->stopSpeaking(); });
    connect(m_speakButton, &QPushButton::clicked, this, [this] { speakMessage(m_edit->toPlainText()); });
    connect(m_stopButton, &QPushButton::clicked, this, [this] { m_ctx->stopSpeaking(); });
    connect(m_phrases, &PhraseGrid::phraseActivated, this, &MainWindow::speakPhrase);

    connect(m_voiceCombo, &QComboBox::activated, this, [this](int index) {
        const QString key = m_voiceCombo->itemData(index).toString();
        if (key == kBrowseVoices) {
            refreshVoiceCombo(); // restore the current selection
            openVoicePicker();
            return;
        }
        if (!key.isEmpty())
            m_ctx->setCurrentVoice(m_ctx->tts()->resolve(key));
    });
    connect(m_ctx, &AppContext::currentVoiceChanged, this, &MainWindow::refreshVoiceCombo);
    connect(m_ctx->tts(), &TtsRegistry::voicesChanged, this, [this] {
        refreshVoiceCombo();
        updateStatus();
    });
    connect(m_speed, &QSlider::valueChanged, this, [this](int v) {
        m_speed->setToolTip(tr("Speaking speed: %1%").arg(v));
        m_ctx->settings()->setValue(Keys::Rate, v);
        m_ctx->applySpeechOptions();
    });

    // Microphone button: behaviour depends on the mode.
    SttController *stt = m_ctx->stt();
    connect(m_micButton, &QPushButton::pressed, this, [this, stt] {
        if (stt->mode() == SttController::Mode::PushToTalk)
            stt->startListening();
    });
    connect(m_micButton, &QPushButton::released, this, [this, stt] {
        if (stt->mode() == SttController::Mode::PushToTalk)
            stt->stopListening();
    });
    connect(m_micButton, &QPushButton::clicked, this, [stt] {
        if (stt->mode() != SttController::Mode::PushToTalk)
            stt->toggleListening();
    });
    connect(stt, &SttController::listeningChanged, this, &MainWindow::updateMicButton);
    connect(stt, &SttController::busyChanged, this, &MainWindow::updateMicButton);
    connect(stt, &SttController::speechActiveChanged, m_level, &LevelMeter::setActive);
    connect(stt, &SttController::levelChanged, m_level, &LevelMeter::setLevel);
    connect(m_ctx, &AppContext::transcriptReady, this, [this](const QString &text) {
        m_edit->insertTranscript(text);
        statusBar()->showMessage(tr("Check the text, then press Enter to speak it."), 6000);
    });

    connect(m_ctx->speech(), &SpeechQueue::speakingChanged, this, [this](bool speaking) {
        m_stopButton->setEnabled(speaking);
    });
    connect(m_ctx->speech(), &SpeechQueue::queueChanged, this, [this](int queued) {
        m_queueLabel->setText(queued > 0 ? tr("%n waiting", nullptr, queued) : QString());
    });
    connect(m_ctx->history(), &QAbstractItemModel::rowsInserted, this, [this] {
        m_edit->setRecallHistory(m_ctx->history()->texts());
    });
    connect(m_history, &QListView::activated, this, [this](const QModelIndex &idx) {
        m_ctx->speak(idx.data(HistoryModel::TextRole).toString());
    });
    connect(m_history, &QListView::customContextMenuRequested, this, &MainWindow::historyMenu);

    connect(m_ctx, &AppContext::notify, this, [this](const QString &msg, int level) {
        if (level >= 2)
            showBanner(msg, level);
        else
            statusBar()->showMessage(msg, 8000);
    });
    connect(m_ctx, &AppContext::quickTypeRequested, this, [this] { m_quickType->popup(); });
    connect(m_ctx, &AppContext::showWindowRequested, this, &MainWindow::showAndRaise);
    connect(m_ctx->obs(), &ObsIntegration::statusChanged, this, &MainWindow::updateStatus);
    connect(m_ctx->overlay(), &OverlayServer::clientCountChanged, this, &MainWindow::updateStatus);
    connect(m_ctx->settings(), &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(Keys::SttMode) || key == QLatin1String(Keys::HotkeyPushToTalk))
            updateMicButton();
        if (key.startsWith(QLatin1String("stt/")) || key.startsWith(QLatin1String("audio/"))
            || key.startsWith(QLatin1String("overlay/")) || key.startsWith(QLatin1String("obs/")))
            QTimer::singleShot(0, this, &MainWindow::updateStatus);
        if (key == QLatin1String(Keys::Rate) && m_speed->value() != m_ctx->settings()->integer(Keys::Rate))
            m_speed->setValue(m_ctx->settings()->integer(Keys::Rate));
        if (key == QLatin1String(Keys::FavoriteVoices))
            refreshVoiceCombo();
    });
    if (SttEngine *engine = m_ctx->sttEngine())
        connect(engine, &SttEngine::readyChanged, this, &MainWindow::updateStatus);

    connect(m_sttChip, &QToolButton::clicked, this, [this] { showSettings(QStringLiteral("stt")); });
    connect(m_obsChip, &QToolButton::clicked, this, [this] { showSettings(QStringLiteral("obs")); });
    connect(m_overlayChip, &QToolButton::clicked, this, [this] { showSettings(QStringLiteral("obs")); });

    // Poll lightweight status (engine readiness changes when models finish loading).
    auto *statusTimer = new QTimer(this);
    statusTimer->setInterval(2000);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer->start();
}

void MainWindow::fitPhrases()
{
    if (!m_phraseScroll)
        return;
    // Grow with the phrases up to about three rows, then scroll.
    const int width = m_phraseScroll->viewport()->width();
    const int wanted = m_phrases->heightForWidth(width) + 4;
    const int maxHeight = fontMetrics().height() * 8;
    m_phraseScroll->setFixedHeight(qBound(40, wanted, maxHeight));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_phraseScroll && watched == m_phraseScroll->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MainWindow::fitPhrases);
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::speakMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_edit->setFocus();
        return;
    }
    const quint64 id = m_ctx->speak(trimmed);
    if (id == 0)
        return;
    m_edit->addVocabulary(TextProcessor::vocabularyFrom(trimmed));
    if (m_ctx->settings()->flag(Keys::ClearAfterSpeak))
        m_edit->clear();
    m_edit->setFocus();
}

void MainWindow::speakPhrase(int index)
{
    const QList<Phrase> &list = m_ctx->phrases()->phrases();
    if (index < 0 || index >= list.size())
        return;
    m_ctx->speak(list.at(index).text, list.at(index).voiceKey);
}

void MainWindow::refreshVoiceCombo()
{
    const Voice current = m_ctx->currentVoice();
    const QStringList favorites = m_ctx->settings()->value(Keys::FavoriteVoices).toStringList();
    auto label = [this](const Voice &v) {
        TtsEngine *e = m_ctx->tts()->engine(v.engineId);
        return QStringLiteral("%1 — %2").arg(v.name, e ? e->displayName() : v.engineId);
    };

    m_voiceCombo->blockSignals(true);
    m_voiceCombo->clear();
    for (const QString &key : favorites) {
        const Voice v = m_ctx->tts()->resolve(key);
        m_voiceCombo->addItem(QStringLiteral("★ ") + label(v), key);
    }
    if (current.isValid() && !favorites.contains(current.key()))
        m_voiceCombo->addItem(label(current), current.key());
    if (m_voiceCombo->count() == 0)
        m_voiceCombo->addItem(tr("No voice yet — choose one…"), kBrowseVoices);
    m_voiceCombo->insertSeparator(m_voiceCombo->count());
    m_voiceCombo->addItem(tr("More voices…"), kBrowseVoices);
    const int idx = m_voiceCombo->findData(current.key());
    m_voiceCombo->setCurrentIndex(qMax(0, idx));
    m_voiceCombo->blockSignals(false);
}

void MainWindow::openVoicePicker()
{
    VoicePickerDialog dlg(m_ctx, this);
    connect(&dlg, &VoicePickerDialog::openSettingsRequested, this, [this](const QString &page) {
        QTimer::singleShot(0, this, [this, page] { showSettings(page); });
    });
    if (dlg.exec() == QDialog::Accepted && dlg.selectedVoice().isValid())
        m_ctx->setCurrentVoice(dlg.selectedVoice());
    refreshVoiceCombo();
}

void MainWindow::updateMicButton()
{
    SttController *stt = m_ctx->stt();
    const bool listening = stt->isListening();
    const bool busy = stt->isBusy();
    const QString hotkey = QKeySequence::fromString(m_ctx->settings()->string(Keys::HotkeyPushToTalk),
                                                    QKeySequence::PortableText)
                               .toString(QKeySequence::NativeText);
    QString text;
    switch (stt->mode()) {
    case SttController::Mode::PushToTalk:
        text = listening ? tr("Listening\u2026 release to finish") : tr("Hold to talk");
        break;
    case SttController::Mode::Toggle:
        text = listening ? tr("Stop and transcribe") : tr("Click to talk");
        break;
    case SttController::Mode::HandsFree:
        text = listening ? tr("Hands-free: on") : tr("Hands-free: off");
        break;
    }
    m_micButton->setText(text);
    m_micButton->setIcon(Icons::mic(listening ? QColor(Qt::white) : palette().color(QPalette::ButtonText)));
    m_micButton->setToolTip(hotkey.isEmpty() ? QString() : tr("Shortcut: %1 (works in any app)").arg(hotkey));
    m_micButton->setProperty("listening", listening);
    m_micButton->style()->unpolish(m_micButton);
    m_micButton->style()->polish(m_micButton);
    m_micStatus->setText(busy ? tr("Writing down what you said…")
                              : listening ? tr("Listening") : QString());
    if (!listening)
        m_level->setLevel(0.0f);
}

void MainWindow::updateStatus()
{
    // Speech recognition chip.
    SttEngine *engine = m_ctx->sttEngine();
    if (!engine) {
        m_sttChip->setText(tr("Speech input: off"));
    } else if (engine->isReady()) {
        m_sttChip->setText(tr("Speech input: %1").arg(engine->displayName().section(QLatin1Char(' '), 0, 0)));
        m_sttChip->setToolTip(tr("Speech recognition is ready"));
    } else {
        m_sttChip->setText(tr("Speech input: set up"));
        m_sttChip->setToolTip(engine->notReadyReason());
    }

    // OBS chip.
    switch (m_ctx->obs()->status()) {
    case ObsIntegration::Status::Disabled:
        m_obsChip->setText(tr("OBS: off"));
        break;
    case ObsIntegration::Status::Connecting:
        m_obsChip->setText(tr("OBS: connecting…"));
        break;
    case ObsIntegration::Status::Connected:
        m_obsChip->setText(tr("● OBS"));
        break;
    case ObsIntegration::Status::AuthFailed:
    case ObsIntegration::Status::Error:
        m_obsChip->setText(tr("⚠ OBS"));
        break;
    }
    m_obsChip->setToolTip(m_ctx->obs()->statusText());

    // Overlay chip.
    OverlayServer *overlay = m_ctx->overlay();
    m_overlayChip->setVisible(overlay->isRunning());
    const int viewers = overlay->clientCount();
    m_overlayChip->setText(viewers > 0 ? tr("● Captions (%1)").arg(viewers) : tr("Captions"));
    m_overlayChip->setToolTip(tr("Caption overlay: %1\n%n browser source(s) connected", nullptr, viewers).arg(m_ctx->overlayUrl()));

    const QStringList outputs = m_ctx->player()->activeDeviceNames();
    m_outputLabel->setText(outputs.isEmpty() ? QString() : tr("Voice → %1").arg(outputs.join(QStringLiteral(" + "))));
}

void MainWindow::showBanner(const QString &message, int level, const QString &fixPage)
{
    QString html = QStringLiteral("%1 %2").arg(level >= 2 ? QStringLiteral("⚠") : QStringLiteral("ℹ"),
                                               message.toHtmlEscaped());
    QString page = fixPage;
    if (page.isEmpty()) {
        // Offer the most likely place to fix common problems.
        if (message.contains(QLatin1String("API key"), Qt::CaseInsensitive) || message.contains(QLatin1String("voice"), Qt::CaseInsensitive))
            page = QStringLiteral("voices");
        else if (message.contains(QLatin1String("model"), Qt::CaseInsensitive) || message.contains(QLatin1String("microphone"), Qt::CaseInsensitive))
            page = QStringLiteral("stt");
        else if (message.contains(QLatin1String("output"), Qt::CaseInsensitive))
            page = QStringLiteral("audio");
    }
    if (!page.isEmpty())
        html += QStringLiteral(" &nbsp;<a href=\"%1\">%2</a>").arg(page, tr("Fix it…"));
    m_banner->setText(html);
    m_banner->show();
    m_bannerTimer->start(12000);
    statusBar()->showMessage(message, 12000);
}

void MainWindow::historyMenu(const QPoint &pos)
{
    const QModelIndex idx = m_history->indexAt(pos);
    QMenu menu(this);
    if (idx.isValid()) {
        const QString text = idx.data(HistoryModel::TextRole).toString();
        menu.addAction(tr("Say again"), this, [this, text] { m_ctx->speak(text); });
        menu.addAction(tr("Edit, then say"), this, [this, text] {
            m_edit->setPlainText(text);
            m_edit->moveCursor(QTextCursor::End);
            m_edit->setFocus();
        });
        menu.addAction(tr("Copy"), this, [text] { QApplication::clipboard()->setText(text); });
        menu.addAction(tr("Save as quick phrase…"), this, [this, text] { m_phrases->addPhrase(text); });
        menu.addSeparator();
    }
    menu.addAction(tr("Clear history"), this, [this] { m_ctx->history()->clear(); });
    menu.exec(m_history->viewport()->mapToGlobal(pos));
}

void MainWindow::showSettings(const QString &page)
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_ctx, this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_settingsDialog, &SettingsDialog::themeChanged, this, &MainWindow::applyTheme);
        connect(m_settingsDialog, &SettingsDialog::setupWizardRequested, this,
                [this] { QTimer::singleShot(0, this, &MainWindow::runSetupWizard); });
        connect(m_settingsDialog, &QDialog::finished, this, [this] {
            refreshVoiceCombo();
            updateStatus();
        });
    }
    if (!page.isEmpty())
        m_settingsDialog->showPage(page);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::runSetupWizard()
{
    SetupWizard wizard(m_ctx, this);
    wizard.exec();
    m_ctx->settings()->setValue(Keys::FirstRunDone, true);
    refreshVoiceCombo();
    updateStatus();
}

void MainWindow::showAndRaise()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
    m_edit->setFocus();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_shownOnce)
        return;
    m_shownOnce = true;
    if (m_firstRunWizard && !m_ctx->settings()->flag(Keys::FirstRunDone))
        QTimer::singleShot(400, this, &MainWindow::runSetupWizard);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_ctx->settings()->setValue(Keys::WindowGeometry, saveGeometry());
    if (!m_quitting && m_tray && m_tray->isVisible() && m_ctx->settings()->flag(Keys::MinimizeToTray)) {
        hide();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(QStringLiteral(VOCALINK_DISPLAY_NAME),
                                tr("Still running in the tray so your shortcuts keep working. "
                                   "Right-click the tray icon to quit."),
                                QSystemTrayIcon::Information, 5000);
        }
        event->ignore();
        return;
    }
    m_quickType->close();
    delete m_quickType;
    m_quickType = nullptr;
    event->accept();
    qApp->quit();
}
