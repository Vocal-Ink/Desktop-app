#include "ui/MessageEdit.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

MessageEdit::MessageEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_completer(new QCompleter(this))
    , m_words(new QStringListModel(this))
{
    setObjectName(QStringLiteral("messageEdit"));
    setPlaceholderText(tr("Type what you want to say and press Enter…"));
    setAccessibleName(tr("Message to speak"));
    setAccessibleDescription(tr("Enter speaks the message. Shift+Enter adds a new line. "
                                "Up recalls earlier messages. Tab completes words. Escape stops speaking."));
    setTabChangesFocus(false);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    m_completer->setModel(m_words);
    m_completer->setWidget(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, &MessageEdit::insertCompletion);
}

void MessageEdit::addVocabulary(const QStringList &words)
{
    bool changed = false;
    for (const QString &w : words) {
        if (w.size() >= 3 && !m_vocabulary.contains(w)) {
            m_vocabulary << w;
            changed = true;
        }
    }
    if (!changed)
        return;
    m_vocabulary.sort(Qt::CaseInsensitive);
    m_words->setStringList(m_vocabulary);
}

void MessageEdit::insertTranscript(const QString &text)
{
    QString current = toPlainText();
    if (!current.isEmpty() && !current.endsWith(QLatin1Char(' ')) && !current.endsWith(QLatin1Char('\n')))
        current += QLatin1Char(' ');
    setPlainText(current + text);
    moveCursor(QTextCursor::End);
    setFocus();
}

QString MessageEdit::wordUnderCursor() const
{
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
    return c.selectedText();
}

void MessageEdit::completeWord()
{
    const QString prefix = wordUnderCursor();
    if (prefix.size() < 2) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    if (m_completer->completionCount() == 1) {
        m_completer->setCurrentRow(0);
        insertCompletion(m_completer->currentCompletion());
        return;
    }
    QRect r = cursorRect();
    r.setWidth(m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24);
    m_completer->complete(r);
}

void MessageEdit::insertCompletion(const QString &completion)
{
    QTextCursor c = textCursor();
    const int already = int(m_completer->completionPrefix().size());
    c.movePosition(QTextCursor::Left);
    c.movePosition(QTextCursor::EndOfWord);
    c.insertText(completion.mid(already) + QLatin1Char(' '));
    setTextCursor(c);
}

void MessageEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab:
        case Qt::Key_Escape:
        case Qt::Key_Backtab:
            event->ignore(); // the completer handles these
            return;
        default:
            break;
        }
    }

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!shift) {
            const QString text = toPlainText().trimmed();
            m_recallIndex = -1;
            if (!text.isEmpty())
                emit submitted(text);
            return;
        }
        break;
    case Qt::Key_Escape:
        emit stopRequested();
        return;
    case Qt::Key_Tab:
        if (event->modifiers() == Qt::NoModifier) {
            completeWord();
            return;
        }
        break;
    case Qt::Key_Up:
        // Recall only when the box is empty or we're already browsing history,
        // so arrow keys still move the cursor in multi-line text.
        if ((toPlainText().isEmpty() || m_recallIndex >= 0) && !m_recall.isEmpty()) {
            m_recallIndex = qMin(m_recallIndex + 1, int(m_recall.size()) - 1);
            setPlainText(m_recall.at(m_recallIndex));
            moveCursor(QTextCursor::End);
            return;
        }
        break;
    case Qt::Key_Down:
        if (m_recallIndex >= 0) {
            --m_recallIndex;
            setPlainText(m_recallIndex >= 0 ? m_recall.at(m_recallIndex) : QString());
            moveCursor(QTextCursor::End);
            return;
        }
        break;
    default:
        if (!event->text().isEmpty())
            m_recallIndex = -1;
        break;
    }
    QPlainTextEdit::keyPressEvent(event);
}
