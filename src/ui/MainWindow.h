#pragma once

#include <QMainWindow>

class AppContext;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppContext *context, QWidget *parent = nullptr);

private:
    AppContext *m_ctx;
};
