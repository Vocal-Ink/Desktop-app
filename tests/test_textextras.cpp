#include "core/TextProcessor.h"

#include <QLocale>
#include <QTest>

using TextProcessor::EmojiMode;
using TextProcessor::UrlMode;

class TestTextExtras : public QObject
{
    Q_OBJECT
private slots:
    void variables()
    {
        TextProcessor::VariableContext ctx;
        ctx.now = QDateTime(QDate(2026, 3, 14), QTime(15, 9));
        ctx.clipboard = QStringLiteral("  copied text \n");
        ctx.voiceName = QStringLiteral("Amy");
        ctx.custom = {{QStringLiteral("Name"), QStringLiteral("Sam")},
                      {QStringLiteral("stream"), QStringLiteral("Tuesday Games")}};
        const QLocale locale;
        const auto expand = [&ctx](const QString &text) { return TextProcessor::expandVariables(text, ctx); };

        QCOMPARE(expand(QStringLiteral("It is {time}.")),
                 QStringLiteral("It is %1.").arg(locale.toString(QTime(15, 9), QLocale::ShortFormat)));
        QCOMPARE(expand(QStringLiteral("{DATE}")), locale.toString(QDate(2026, 3, 14), QLocale::LongFormat));
        QCOMPARE(expand(QStringLiteral("Happy {Day}!")),
                 QStringLiteral("Happy %1!").arg(locale.dayName(QDate(2026, 3, 14).dayOfWeek(), QLocale::LongFormat)));
        QCOMPARE(expand(QStringLiteral("You said: {clipboard}")), QStringLiteral("You said: copied text"));
        QCOMPARE(expand(QStringLiteral("This is {voice}")), QStringLiteral("This is Amy"));
        QCOMPARE(expand(QStringLiteral("I'm {name}, welcome to {Stream}. {NAME}!")),
                 QStringLiteral("I'm Sam, welcome to Tuesday Games. Sam!"));
        QCOMPARE(expand(QStringLiteral("Hi { name }")), QStringLiteral("Hi Sam"));

        // Unknown tokens, escapes and stray braces stay as typed.
        QCOMPARE(expand(QStringLiteral("{nope} and {} and {time")), QStringLiteral("{nope} and {} and {time"));
        QCOMPARE(expand(QStringLiteral("{{time}} is {{literal")), QStringLiteral("{time} is {literal"));
        QCOMPARE(expand(QStringLiteral("{{name}")), QStringLiteral("{name}"));
        QCOMPARE(expand(QStringLiteral("a {x{name}} b")), QStringLiteral("a {xSam} b"));
        QCOMPARE(expand(QStringLiteral("no variables here")), QStringLiteral("no variables here"));

        // Values are inserted verbatim, not expanded again; custom names win.
        ctx.clipboard = QStringLiteral("{name}");
        QCOMPARE(expand(QStringLiteral("{clipboard}")), QStringLiteral("{name}"));
        ctx.custom.insert(QStringLiteral("time"), QStringLiteral("snack time"));
        QCOMPARE(expand(QStringLiteral("{time}")), QStringLiteral("snack time"));

        // No time given: now.
        TextProcessor::VariableContext current;
        QVERIFY(!TextProcessor::expandVariables(QStringLiteral("{day}"), current).contains(QLatin1Char('{')));
    }

    void emojiAreSpoken()
    {
        const auto speak = [](const QString &text) { return TextProcessor::handleEmoji(text, EmojiMode::Speak); };
        QCOMPARE(speak(QStringLiteral(u"I \u2764\uFE0F you")), QStringLiteral("I heart you"));
        QCOMPARE(speak(QStringLiteral(u"I \u2764 you")), QStringLiteral("I heart you"));
        QCOMPARE(speak(QStringLiteral(u"that was great\U0001F602")), QStringLiteral("that was great laughing"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F44D")), QStringLiteral("thumbs up"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F525lets go")), QStringLiteral("fire lets go"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F62D\U0001F64F")), QStringLiteral("crying please"));
        QCOMPARE(speak(QStringLiteral(u"Good job! \U0001F44D!")), QStringLiteral("Good job! thumbs up!"));
    }

    void repeatedEmojiCollapse()
    {
        const auto speak = [](const QString &text) { return TextProcessor::handleEmoji(text, EmojiMode::Speak); };
        QCOMPARE(speak(QStringLiteral(u"\U0001F602\U0001F602\U0001F602")), QStringLiteral("laughing"));
        QCOMPARE(speak(QStringLiteral(u"lol \U0001F602 \U0001F602 \U0001F602 ok")), QStringLiteral("lol laughing ok"));
        // Same word from different emoji collapses too; different words don't.
        QCOMPARE(speak(QStringLiteral(u"\U0001F602\U0001F606")), QStringLiteral("laughing"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F602 hi \U0001F602")), QStringLiteral("laughing hi laughing"));
    }

    void emojiSequences()
    {
        const auto speak = [](const QString &text) { return TextProcessor::handleEmoji(text, EmojiMode::Speak); };
        // Skin tones don't change the meaning.
        QCOMPARE(speak(QStringLiteral(u"nice \U0001F44D\U0001F3FD")), QStringLiteral("nice thumbs up"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F44B\U0001F3FF\U0001F44B\U0001F3FB")), QStringLiteral("wave"));
        // ZWJ sequences: known ones by name, others by their parts.
        QCOMPARE(speak(QStringLiteral(u"\u2764\uFE0F\u200D\U0001F525")), QStringLiteral("heart on fire"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F3F3\uFE0F\u200D\U0001F308")), QStringLiteral("rainbow flag"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F468\u200D\U0001F469\u200D\U0001F467")), QStringLiteral("man woman girl"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F926\U0001F3FB\u200D\u2640\uFE0F why")), QStringLiteral("facepalm why"));
        // Flags, keycaps, unmapped emoji and stray modifiers.
        QCOMPARE(speak(QStringLiteral(u"go \U0001F1FA\U0001F1F8")), QStringLiteral("go flag"));
        QCOMPARE(speak(QStringLiteral(u"\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F")),
                 QStringLiteral("flag"));
        QCOMPARE(speak(QStringLiteral(u"press 1\uFE0F\u20E3 now")), QStringLiteral("press 1 now"));
        QCOMPARE(speak(QStringLiteral(u"rawr \U0001F996")), QStringLiteral("rawr"));
        QCOMPARE(speak(QStringLiteral(u"ok\uFE0F")), QStringLiteral("ok"));
    }

    void emojiAreRemoved()
    {
        const auto remove = [](const QString &text) { return TextProcessor::handleEmoji(text, EmojiMode::Remove); };
        QCOMPARE(remove(QStringLiteral(u"I \u2764\uFE0F you")), QStringLiteral("I you"));
        QCOMPARE(remove(QStringLiteral(u"great \U0001F44D\U0001F3FD!")), QStringLiteral("great!"));
        QCOMPARE(remove(QStringLiteral(u"\U0001F468\u200D\U0001F469\u200D\U0001F467 family")), QStringLiteral("family"));
        QCOMPARE(remove(QStringLiteral(u"hi\U0001F602there")), QStringLiteral("hi there"));
        QCOMPARE(remove(QStringLiteral(u"\U0001F602\U0001F602\U0001F602")), QString());
        QCOMPARE(remove(QStringLiteral(u"go \U0001F1FA\U0001F1F8 \U0001F3F3\uFE0F\u200D\U0001F308 go")),
                 QStringLiteral("go go"));
        QCOMPARE(remove(QStringLiteral(u"press 1\uFE0F\u20E3")), QStringLiteral("press 1"));
        QCOMPARE(remove(QStringLiteral(u"skin\U0001F3FD tone")), QStringLiteral("skin tone"));
    }

    void textWithoutEmojiIsUntouched()
    {
        for (const QString &text : {QStringLiteral("  plain  text  "), QStringLiteral(u"caf\u00E9 \u2192 na\u00EFve \u2122 \u00A9"),
                                    QStringLiteral(u"\u0915\u094D\u200D\u0937"), QStringLiteral(u"\u3053\u3093\u306B\u3061\u306F"),
                                    QStringLiteral("1 + 2 = 3 #tag *star*")}) {
            QCOMPARE(TextProcessor::handleEmoji(text, EmojiMode::Speak), text);
            QCOMPARE(TextProcessor::handleEmoji(text, EmojiMode::Remove), text);
        }
        const QString emoji = QStringLiteral(u"I \u2764\uFE0F you \U0001F602\U0001F602");
        QCOMPARE(TextProcessor::handleEmoji(emoji, EmojiMode::Keep), emoji);
    }

    void urls()
    {
        const auto say = [](const QString &text) { return TextProcessor::handleUrls(text, UrlMode::SayLink); };
        const auto remove = [](const QString &text) { return TextProcessor::handleUrls(text, UrlMode::Remove); };
        QCOMPARE(say(QStringLiteral("look at https://example.com/a?b=1&c=2#x now")), QStringLiteral("look at link now"));
        QCOMPARE(say(QStringLiteral("go to www.twitch.tv/someone.")), QStringLiteral("go to link."));
        QCOMPARE(say(QStringLiteral("(see http://x.com/wiki/Foo_(bar))")), QStringLiteral("(see link)"));
        QCOMPARE(say(QStringLiteral("HTTPS://EXAMPLE.COM and https://b.org")), QStringLiteral("link and link"));
        QCOMPARE(remove(QStringLiteral("check https://example.com out")), QStringLiteral("check out"));
        QCOMPARE(remove(QStringLiteral("see https://x.com/y, ok")), QStringLiteral("see, ok"));
        QCOMPARE(remove(QStringLiteral("pics (https://x.com/p) here")), QStringLiteral("pics here"));
        QCOMPARE(remove(QStringLiteral("https://only.link")), QString());

        const QString text = QStringLiteral("visit https://example.com now");
        QCOMPARE(TextProcessor::handleUrls(text, UrlMode::Keep), text);
        for (const QString &plain : {QStringLiteral("e.g. 1.5 apples"), QStringLiteral("wwwdotcom"),
                                     QStringLiteral("the http protocol"), QStringLiteral("mail me@www.example.com")})
            QCOMPARE(say(plain), plain);
    }

    void capitalizes()
    {
        const auto cap = [](const QString &text) { return TextProcessor::autoCapitalize(text); };
        QCOMPARE(cap(QStringLiteral("hi. ok")), QStringLiteral("Hi. Ok"));
        QCOMPARE(cap(QStringLiteral("i think i'm fine and i'll go")), QStringLiteral("I think I'm fine and I'll go"));
        QCOMPARE(cap(QStringLiteral(u"so i\u2019d say")), QStringLiteral(u"So I\u2019d say"));
        QCOMPARE(cap(QStringLiteral("what? really! yes")), QStringLiteral("What? Really! Yes"));
        QCOMPARE(cap(QStringLiteral("so... i guess")), QStringLiteral("So... I guess"));
        QCOMPARE(cap(QStringLiteral("we can, e.g. this. or i.e. that")), QStringLiteral("We can, e.g. this. Or i.e. that"));
        QCOMPARE(cap(QStringLiteral("hello World, my iPhone is OK")), QStringLiteral("Hello World, my iPhone is OK"));
        QCOMPARE(cap(QStringLiteral("first line\nsecond line")), QStringLiteral("First line\nSecond line"));
        QCOMPARE(cap(QStringLiteral("\"hey\" she said. \"ok\"")), QStringLiteral("\"Hey\" she said. \"Ok\""));
        QCOMPARE(cap(QStringLiteral("it's 3.5 km. then ask mr. smith")), QStringLiteral("It's 3.5 km. Then ask mr. smith"));
        QCOMPARE(cap(QStringLiteral("ipad imac")), QStringLiteral("Ipad imac"));
        QCOMPARE(cap(QStringLiteral("  already Fine.")), QStringLiteral("  Already Fine."));
        QCOMPARE(cap(QString()), QString());
    }
};

QTEST_GUILESS_MAIN(TestTextExtras)
#include "test_textextras.moc"
