#include "ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace Theme {

namespace {

struct Colors
{
    QString window, base, alt, text, muted, border, button, buttonHover, accent, accentText, focus, danger;
};

Colors colorsFor(const QString &name)
{
    if (name == QLatin1String("light"))
        return {QStringLiteral("#f4f5fb"), QStringLiteral("#ffffff"), QStringLiteral("#eceef7"),
                QStringLiteral("#1b1d2a"), QStringLiteral("#5b5f78"), QStringLiteral("#cfd3e6"),
                QStringLiteral("#e6e8f4"), QStringLiteral("#dcdff0"), QStringLiteral("#5a45f0"),
                QStringLiteral("#ffffff"), QStringLiteral("#5a45f0"), QStringLiteral("#c62828")};
    if (name == QLatin1String("contrast"))
        return {QStringLiteral("#000000"), QStringLiteral("#000000"), QStringLiteral("#101010"),
                QStringLiteral("#ffffff"), QStringLiteral("#f0f0f0"), QStringLiteral("#ffffff"),
                QStringLiteral("#000000"), QStringLiteral("#1f1f1f"), QStringLiteral("#ffe600"),
                QStringLiteral("#000000"), QStringLiteral("#00e5ff"), QStringLiteral("#ff5252")};
    return {QStringLiteral("#15161f"), QStringLiteral("#1d1f2b"), QStringLiteral("#232536"),
            QStringLiteral("#eceefa"), QStringLiteral("#a2a6c2"), QStringLiteral("#33364a"),
            QStringLiteral("#272a3b"), QStringLiteral("#31354b"), QStringLiteral("#7b6cff"),
            QStringLiteral("#ffffff"), QStringLiteral("#9d92ff"), QStringLiteral("#ff6b6b")};
}

QString s_accent = QStringLiteral("#7b6cff");

} // namespace

QString accent()
{
    return s_accent;
}

void apply(QApplication *app, const QString &name, int fontScalePercent)
{
    const Colors c = colorsFor(name);
    s_accent = c.accent;
    const bool contrast = name == QLatin1String("contrast");

    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, QColor(c.window));
    p.setColor(QPalette::WindowText, QColor(c.text));
    p.setColor(QPalette::Base, QColor(c.base));
    p.setColor(QPalette::AlternateBase, QColor(c.alt));
    p.setColor(QPalette::Text, QColor(c.text));
    p.setColor(QPalette::PlaceholderText, QColor(c.muted));
    p.setColor(QPalette::Button, QColor(c.button));
    p.setColor(QPalette::ButtonText, QColor(c.text));
    p.setColor(QPalette::Highlight, QColor(c.accent));
    p.setColor(QPalette::HighlightedText, QColor(c.accentText));
    p.setColor(QPalette::ToolTipBase, QColor(c.alt));
    p.setColor(QPalette::ToolTipText, QColor(c.text));
    p.setColor(QPalette::Link, QColor(c.focus));
    p.setColor(QPalette::BrightText, QColor(c.danger));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c.muted));
    app->setPalette(p);

    QFont f = QApplication::font();
    static const qreal basePoint = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
    f.setPointSizeF(basePoint * qBound(80, fontScalePercent, 250) / 100.0);
    app->setFont(f);

    const QString focusWidth = contrast ? QStringLiteral("3px") : QStringLiteral("2px");
    QString qss = QStringLiteral(R"(
* { dialogbuttonbox-buttons-have-icons: 0; }
QWidget { color: %TEXT%; }
QMainWindow, QDialog, QWizard { background: %WINDOW%; }
QToolTip { background: %ALT%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px; }
QGroupBox { border: 1px solid %BORDER%; border-radius: 10px; margin-top: 1.2em; padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: %MUTED%; font-weight: 600; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QKeySequenceEdit {
    background: %BASE%; border: 1px solid %BORDER%; border-radius: 8px; padding: 5px 8px;
    selection-background-color: %ACCENT%; selection-color: %ACCENTTEXT%;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus {
    border: %FOCUSW% solid %FOCUS%;
}
QComboBox QAbstractItemView { background: %BASE%; border: 1px solid %BORDER%; selection-background-color: %ACCENT%; }
QPushButton, QToolButton {
    background: %BUTTON%; border: 1px solid %BORDER%; border-radius: 8px; padding: 6px 12px;
}
QPushButton:hover, QToolButton:hover { background: %BUTTONHOVER%; }
QPushButton:focus, QToolButton:focus { border: %FOCUSW% solid %FOCUS%; }
QPushButton:pressed, QToolButton:pressed, QPushButton:checked, QToolButton:checked { background: %ACCENT%; color: %ACCENTTEXT%; }
QPushButton:disabled, QToolButton:disabled { color: %MUTED%; }
QPushButton[primary="true"] { background: %ACCENT%; color: %ACCENTTEXT%; border: none; font-weight: 600; padding: 8px 18px; }
QPushButton[primary="true"]:hover { background: %FOCUS%; }
QPushButton[danger="true"] { color: %DANGER%; }
QPushButton[phrase="true"] { padding: 8px 14px; border-radius: 14px; text-align: left; }
QPushButton#micButton { border-radius: 22px; padding: 10px 18px; font-weight: 600; }
QPushButton#micButton[listening="true"] { background: %DANGER%; color: #ffffff; border: none; }
QListView, QTreeView, QTableView, QTreeWidget {
    background: %BASE%; alternate-background-color: %ALT%; border: 1px solid %BORDER%; border-radius: 8px;
    selection-background-color: %ACCENT%; selection-color: %ACCENTTEXT%;
}
QListView::item { padding: 7px 8px; border-bottom: 1px solid %BORDER%; }
QListView::item:selected { background: %ACCENT%; color: %ACCENTTEXT%; }
QHeaderView::section { background: %ALT%; color: %MUTED%; border: none; padding: 6px; }
QTabWidget::pane { border: 1px solid %BORDER%; border-radius: 8px; top: -1px; }
QTabBar::tab { background: transparent; padding: 8px 14px; color: %MUTED%; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %TEXT%; border-bottom: 2px solid %ACCENT%; }
QToolBar { background: %WINDOW%; border: none; spacing: 8px; padding: 6px; }
QStatusBar { color: %MUTED%; }
QSlider::groove:horizontal { height: 6px; background: %BORDER%; border-radius: 3px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 3px; }
QSlider::handle:horizontal { background: %TEXT%; width: 16px; margin: -6px 0; border-radius: 8px; }
QProgressBar { background: %BASE%; border: 1px solid %BORDER%; border-radius: 6px; text-align: center; height: 14px; }
QProgressBar::chunk { background: %ACCENT%; border-radius: 5px; }
QScrollArea { border: none; background: transparent; }
QLabel[hint="true"] { color: %MUTED%; }
QToolButton[chip="true"] { border: 1px solid %BORDER%; border-radius: 12px; padding: 3px 10px; color: %MUTED%; background: transparent; }
QToolButton[chip="true"]:hover { color: %TEXT%; border-color: %ACCENT%; }
QLabel[chip="true"] { border: 1px solid %BORDER%; border-radius: 10px; padding: 2px 8px; color: %MUTED%; }
QLabel[chip="ok"] { border: 1px solid %ACCENT%; border-radius: 10px; padding: 2px 8px; }
QLabel[chip="bad"] { border: 1px solid %DANGER%; border-radius: 10px; padding: 2px 8px; color: %DANGER%; }
QFrame#card { background: %BASE%; border: 1px solid %BORDER%; border-radius: 12px; }
QPlainTextEdit#messageEdit { font-size: %MSGSIZE%pt; padding: 10px; border-radius: 12px; }
QLineEdit#quickType { font-size: %QTSIZE%pt; padding: 12px 16px; border-radius: 14px; border: 2px solid %ACCENT%; }
QFrame#quickTypeFrame { background: %WINDOW%; border: 1px solid %BORDER%; border-radius: 18px; }
)");
    qss.replace(QLatin1String("%TEXT%"), c.text)
        .replace(QLatin1String("%WINDOW%"), c.window)
        .replace(QLatin1String("%BASE%"), c.base)
        .replace(QLatin1String("%ALT%"), c.alt)
        .replace(QLatin1String("%MUTED%"), c.muted)
        .replace(QLatin1String("%BORDER%"), c.border)
        .replace(QLatin1String("%BUTTONHOVER%"), c.buttonHover)
        .replace(QLatin1String("%BUTTON%"), c.button)
        .replace(QLatin1String("%ACCENTTEXT%"), c.accentText)
        .replace(QLatin1String("%ACCENT%"), c.accent)
        .replace(QLatin1String("%FOCUSW%"), focusWidth)
        .replace(QLatin1String("%FOCUS%"), c.focus)
        .replace(QLatin1String("%DANGER%"), c.danger)
        .replace(QLatin1String("%MSGSIZE%"), QString::number(qRound(f.pointSizeF() * 1.45)))
        .replace(QLatin1String("%QTSIZE%"), QString::number(qRound(f.pointSizeF() * 1.6)));
    app->setStyleSheet(qss);
}

} // namespace Theme
