#include "ui/MainWindow.h"

#include "app/AppContext.h"

MainWindow::MainWindow(AppContext *context, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Vocal Ink"));
    resize(900, 600);
}
