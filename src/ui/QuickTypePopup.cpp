#include "ui/QuickTypePopup.h"

#include "app/AppContext.h"
#include "core/HistoryModel.h"

#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QVBoxLayout>

QuickTypePopup::QuickTypePopup(AppContext *context, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_ctx(context)
    , m_edit(new QLineEdit(this))
    , m_voice(new QLabel(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(tr("Vocal Ink quick type"));

    auto *frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("quickTypeFrame"));
    m_edit->setObjectName(QStringLiteral("quickType"));
    m_edit->setPlaceholderText(tr("Type and press Enter to speak • Esc to close"));
    m_edit->setAccessibleName(tr("Quick message"));
    m_edit->installEventFilter(this);
    m_voice->setProperty("hint", true);

    auto *inner = new QVBoxLayout(frame);
    inner->setContentsMargins(14, 14, 14, 10);
    inner->addWidget(m_edit);
    inner->addWidget(m_voice);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);

    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_edit->text().trimmed();
        if (!text.isEmpty())
            m_ctx->speak(text);
        m_edit->clear();
        hide();
    });
}

void QuickTypePopup::popup()
{
    const Voice v = m_ctx->currentVoice();
    m_voice->setText(v.isValid() ? tr("Voice: %1 • ↑ recalls earlier messages").arg(v.name)
                                 : tr("No voice selected yet"));
    m_recallIndex = -1;

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    const int w = qMin(760, area.width() - 80);
    resize(w, sizeHint().height());
    move(area.center().x() - w / 2, area.top() + area.height() / 3);
    show();
    raise();
    activateWindow();
    m_edit->setFocus(Qt::ActiveWindowFocusReason);
}

bool QuickTypePopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QStringList recall = m_ctx->history()->texts();
        switch (key->key()) {
        case Qt::Key_Escape:
            m_edit->clear();
            hide();
            return true;
        case Qt::Key_Up:
            if (!recall.isEmpty()) {
                m_recallIndex = qMin(m_recallIndex + 1, int(recall.size()) - 1);
                m_edit->setText(recall.at(m_recallIndex));
            }
            return true;
        case Qt::Key_Down:
            if (m_recallIndex >= 0) {
                --m_recallIndex;
                m_edit->setText(m_recallIndex >= 0 ? recall.at(m_recallIndex) : QString());
            }
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuickTypePopup::changeEvent(QEvent *event)
{
    // Clicking elsewhere dismisses the box (keeps typed text for next time).
    if (event->type() == QEvent::ActivationChange && !isActiveWindow() && isVisible())
        hide();
    QWidget::changeEvent(event);
}
