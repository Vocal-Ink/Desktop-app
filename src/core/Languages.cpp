#include "core/Languages.h"

#include <QHash>

QStringList Languages::supported()
{
    return {QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("de"),
            QStringLiteral("pt_BR"), QStringLiteral("it"), QStringLiteral("nl"), QStringLiteral("pl"),
            QStringLiteral("tr"), QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("zh_CN")};
}

QString Languages::match(const QStringList &uiLanguages)
{
    const QStringList codes = supported();
    for (const QString &raw : uiLanguages) {
        const QString tag = QString(raw).replace(QLatin1Char('-'), QLatin1Char('_'));
        const QString lang = tag.section(QLatin1Char('_'), 0, 0).toLower();
        if (lang == QLatin1String("zh")) {
            // Simplified only: zh, zh_CN, zh_SG, zh_Hans...; not zh_TW/HK/MO or Hant.
            const QString rest = tag.mid(2).toLower();
            if (rest.contains(QLatin1String("hant")) || rest.contains(QLatin1String("_tw"))
                || rest.contains(QLatin1String("_hk")) || rest.contains(QLatin1String("_mo")))
                continue;
            return QStringLiteral("zh_CN");
        }
        if (lang == QLatin1String("pt"))
            return QStringLiteral("pt_BR");
        if (codes.contains(lang))
            return lang;
    }
    return QStringLiteral("en");
}

QString Languages::nativeName(const QString &code)
{
    // Written out rather than taken from QLocale, whose casing and naming vary
    // between Qt versions ("español", "中文").
    static const QHash<QString, QString> names = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
    };
    return names.value(code, code);
}
