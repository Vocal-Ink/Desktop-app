#pragma once

#include <QWizard>

class AppContext;

// First-run assistant: voice output routing, a local voice, speech recognition
// and (optionally) cloud voice keys. Everything can be skipped and changed later.
class SetupWizard : public QWizard
{
    Q_OBJECT
public:
    explicit SetupWizard(AppContext *context, QWidget *parent = nullptr);

    void accept() override;

private:
    QWizardPage *welcomePage();
    QWizardPage *outputPage();
    QWizardPage *voicePage();
    QWizardPage *sttPage();
    QWizardPage *cloudPage();
    QWizardPage *donePage();

    AppContext *m_ctx;
};
