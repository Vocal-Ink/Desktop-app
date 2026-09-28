#include "ui/LanguageManager.h"

#include "core/Languages.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QTranslator>

namespace {

// Fonts to try for CJK text before the system's own fallback, so Chinese
// isn't drawn with Japanese glyph shapes (and the other way round).
QStringList cjkFonts(const QString &code)
{
    if (code == QLatin1String("zh_CN"))
        return {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("PingFang SC"),
                QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Source Han Sans SC")};
    if (code == QLatin1String("ja"))
        return {QStringLiteral("Yu Gothic UI"), QStringLiteral("Meiryo UI"), QStringLiteral("Hiragino Sans"),
                QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Source Han Sans JP")};
    if (code == QLatin1String("ko"))
        return {QStringLiteral("Malgun Gothic"), QStringLiteral("Apple SD Gothic Neo"),
                QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Source Han Sans KR")};
    return {};
}

const QStringList &appFontFamilies()
{
    static const QStringList families = {QStringLiteral("Atkinson Hyperlegible Next"),
                                         QStringLiteral("Bricolage Grotesque"), QStringLiteral("Vocal Ink Display"),
                                         QStringLiteral("Lexend"), QStringLiteral("OpenDyslexic")};
    return families;
}

} // namespace

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
    Settings settings; // same store AppContext will open
    m_choice = settings.string(Keys::Language);
    apply(m_choice.isEmpty() ? systemLanguage() : m_choice);
}

LanguageManager::~LanguageManager()
{
    removeTranslators();
}

QString LanguageManager::systemLanguage() const
{
    return Languages::match(QLocale::system().uiLanguages());
}

QVariantList LanguageManager::available() const
{
    QVariantList out;
    for (const QString &code : Languages::supported()) {
        const QLocale locale(code);
        out.append(QVariantMap{{QStringLiteral("code"), code},
                               {QStringLiteral("name"), Languages::nativeName(code)},
                               {QStringLiteral("english"), QLocale::languageToString(locale.language())}});
    }
    return out;
}

void LanguageManager::attach(Settings *settings, QQmlEngine *engine)
{
    m_settings = settings;
    m_engine = engine;
    if (m_engine)
        m_engine->setUiLanguage(m_current);
    connect(settings, &Settings::changed, this, [this](const QString &key) {
        if (key != QLatin1String(Keys::Language))
            return;
        m_choice = m_settings->string(Keys::Language);
        if (m_override.isEmpty())
            apply(m_choice.isEmpty() ? systemLanguage() : m_choice);
        else
            emit languageChanged(); // the choice changed, the language in use didn't
    });
}

void LanguageManager::setOverride(const QString &code)
{
    m_override = Languages::supported().contains(code) ? code : Languages::match({code});
    apply(m_override);
}

void LanguageManager::choose(const QString &code)
{
    if (m_settings)
        m_settings->setValue(Keys::Language, Languages::supported().contains(code) ? code : QString());
}

void LanguageManager::removeTranslators()
{
    for (QTranslator *t : std::as_const(m_translators)) {
        QCoreApplication::removeTranslator(t);
        delete t;
    }
    m_translators.clear();
}

void LanguageManager::apply(const QString &code)
{
    if (code == m_current && !m_translators.isEmpty())
        return;
    removeTranslators();

    const auto load = [this](const QString &name) {
        auto *t = new QTranslator;
        const QString bundled = QStringLiteral(":/i18n/") + name + QStringLiteral(".qm");
        const bool ok = QFile::exists(bundled)
            ? t->load(bundled)
            : t->load(name, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
        if (ok && QCoreApplication::installTranslator(t))
            m_translators.append(t);
        else
            delete t;
    };
    // English still gets its file: it holds the plural forms ("1 voice", "2 voices").
    load(QStringLiteral("vocalink_") + code);
    if (code != QLatin1String("en")) {
        load(QStringLiteral("qtbase_") + code);
        load(QStringLiteral("qtdeclarative_") + code);
    }

    for (const QString &family : appFontFamilies()) {
        QFont::removeSubstitutions(family);
        const QStringList cjk = cjkFonts(code);
        if (!cjk.isEmpty())
            QFont::insertSubstitutions(family, cjk);
    }
    QGuiApplication::setLayoutDirection(QLocale(code).textDirection());

    m_current = code;
    if (m_engine) {
        m_engine->setUiLanguage(code);
        m_engine->retranslate();
    }
    emit languageChanged();
}
