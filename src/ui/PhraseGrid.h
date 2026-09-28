#pragma once

#include "core/PhraseStore.h"

#include <QDialog>
#include <QWidget>

class FlowLayout;
class QComboBox;
class QKeySequenceEdit;
class QPlainTextEdit;
class TtsRegistry;

// One-click quick phrases ("Yes", "No", "One moment"...), editable in place.
class PhraseGrid : public QWidget
{
    Q_OBJECT
public:
    PhraseGrid(PhraseStore *store, TtsRegistry *registry, QWidget *parent = nullptr);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

public slots:
    void addPhrase(const QString &initialText = QString());

signals:
    void phraseActivated(int index);

private:
    void rebuild();
    void editPhrase(int index);
    void showMenu(int index, const QPoint &globalPos);

    PhraseStore *m_store;
    TtsRegistry *m_registry;
    FlowLayout *m_flow;
};

class PhraseEditDialog : public QDialog
{
    Q_OBJECT
public:
    PhraseEditDialog(const Phrase &phrase, TtsRegistry *registry, QWidget *parent = nullptr);
    Phrase phrase() const;

private:
    QPlainTextEdit *m_text;
    QKeySequenceEdit *m_hotkey;
    QComboBox *m_voice;
};
