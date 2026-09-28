#pragma once

#include <QPlainTextEdit>
#include <QStringList>

class QCompleter;
class QStringListModel;

// The main "what do you want to say?" box.
//  Enter           speak            Shift+Enter  new line
//  Up / Down       recall earlier messages (when the box is empty or recalling)
//  Tab             complete the current word from your own vocabulary
//  Esc             stop speaking
class MessageEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MessageEdit(QWidget *parent = nullptr);

    void setRecallHistory(const QStringList &newestFirst) { m_recall = newestFirst; }
    void addVocabulary(const QStringList &words);
    void insertTranscript(const QString &text);

signals:
    void submitted(const QString &text);
    void stopRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QString wordUnderCursor() const;
    void completeWord();
    void insertCompletion(const QString &completion);

    QStringList m_recall;
    int m_recallIndex = -1;
    QCompleter *m_completer;
    QStringListModel *m_words;
    QStringList m_vocabulary;
};
