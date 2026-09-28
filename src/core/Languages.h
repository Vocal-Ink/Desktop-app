#pragma once

#include <QString>
#include <QStringList>

// The interface languages Vocal Ink ships (i18n/vocalink_<code>.ts).
namespace Languages {

QStringList supported(); // en es fr de pt_BR it nl pl tr ja ko zh_CN
// The best supported code for a list like QLocale::uiLanguages()
// ("de-AT" -> "de", "zh-Hans-CN" -> "zh_CN", "pt-PT" -> "pt_BR"; never
// Traditional Chinese). "en" when nothing matches.
QString match(const QStringList &uiLanguages);
QString nativeName(const QString &code); // "Deutsch", "日本語"...

} // namespace Languages
