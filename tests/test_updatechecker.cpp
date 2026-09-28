#include "Version.h"
#include "core/UpdateChecker.h"
#include "support/MockHttpServer.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>

namespace {

QByteArray release(const QString &tag, bool prerelease = false, bool draft = false)
{
    return QStringLiteral("{\"tag_name\":\"%1\",\"html_url\":\"https://github.com/Vocal-Ink/Desktop-app/releases/tag/%1\","
                          "\"body\":\"* Faster voices\\n* Fixes\",\"draft\":%2,\"prerelease\":%3}")
        .arg(tag, draft ? QStringLiteral("true") : QStringLiteral("false"),
             prerelease ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

} // namespace

class TestUpdateChecker : public QObject
{
    Q_OBJECT
private slots:
    void comparesVersions_data()
    {
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("newer");
        QTest::newRow("patch") << "1.2.10" << "1.2.9" << true;
        QTest::newRow("v prefix") << "v1.2.10" << "1.2.9" << true;
        QTest::newRow("both prefixed") << "V2.0.0" << "v1.9.9" << true;
        QTest::newRow("same") << "v1.2.3" << "1.2.3" << false;
        QTest::newRow("older") << "1.2.3" << "1.10.0" << false;
        QTest::newRow("missing parts") << "1.3" << "1.2.9" << true;
        QTest::newRow("missing parts equal") << "1.2" << "1.2.0" << false;
        QTest::newRow("minor") << "0.2.0" << "0.1.99" << true;
        QTest::newRow("release beats beta") << "1.3.0" << "1.3.0-beta.2" << true;
        QTest::newRow("beta is older") << "1.3.0-beta.2" << "1.3.0" << false;
        QTest::newRow("beta numbers") << "1.3.0-beta.10" << "1.3.0-beta.2" << true;
        QTest::newRow("rc after beta") << "1.3.0-rc.1" << "1.3.0-beta.5" << true;
        QTest::newRow("longer pre-release") << "1.3.0-beta.1.1" << "1.3.0-beta.1" << true;
        QTest::newRow("beta of next") << "1.4.0-beta.1" << "1.3.9" << true;
        QTest::newRow("build metadata") << "1.2.3+build.7" << "1.2.3" << false;
        QTest::newRow("whitespace") << " v1.0.1 " << "1.0.0" << true;
        QTest::newRow("garbage candidate") << "latest" << "1.0.0" << false;
        QTest::newRow("empty") << "" << "1.0.0" << false;
        QTest::newRow("garbage current") << "1.0.0" << "dev" << false;
    }

    void comparesVersions()
    {
        QFETCH(QString, candidate);
        QFETCH(QString, current);
        QFETCH(bool, newer);
        QCOMPARE(UpdateChecker::isNewer(candidate, current), newer);
    }

    void reportsNewerRelease()
    {
        MockHttpServer server;
        QVERIFY(server.listen());
        server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(release(QStringLiteral("v99.1.0")));
        });
        QNetworkAccessManager network;
        UpdateChecker checker(&network);
        checker.setApiUrl(server.baseUrl() + QStringLiteral("/repos/x/y/releases/latest"));
        QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
        QSignalSpy upToDate(&checker, &UpdateChecker::upToDate);
        QSignalSpy failed(&checker, &UpdateChecker::failed);
        checker.check();
        QVERIFY(checker.isChecking());
        checker.check(); // ignored while running
        QVERIFY(available.wait(5000));
        QCOMPARE(available.first().at(0).toString(), QStringLiteral("99.1.0"));
        QCOMPARE(available.first().at(1).toUrl(),
                 QUrl(QStringLiteral("https://github.com/Vocal-Ink/Desktop-app/releases/tag/v99.1.0")));
        QVERIFY(available.first().at(2).toString().contains(QStringLiteral("Faster voices")));
        QCOMPARE(upToDate.size(), 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(!checker.isChecking());

        QCOMPARE(server.requests().size(), 1);
        const MockHttpServer::Request &req = server.requests().first();
        QCOMPARE(req.path, QStringLiteral("/repos/x/y/releases/latest"));
        QCOMPARE(req.header("accept"), QByteArray("application/vnd.github+json"));
        QVERIFY(req.header("user-agent").startsWith("VocalInk/"));
    }

    void sameOrPrereleaseIsUpToDate()
    {
        MockHttpServer server;
        QVERIFY(server.listen());
        QByteArray body = release(QStringLiteral("v") + QStringLiteral(VOCALINK_VERSION));
        server.setHandler([&body](const MockHttpServer::Request &) { return MockHttpServer::Response::json(body); });
        QNetworkAccessManager network;
        UpdateChecker checker(&network);
        checker.setApiUrl(server.baseUrl() + QStringLiteral("/latest"));
        QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
        QSignalSpy upToDate(&checker, &UpdateChecker::upToDate);

        checker.check();
        QVERIFY(upToDate.wait(5000));

        body = release(QStringLiteral("v99.0.0-beta.1"), true);
        checker.check();
        QVERIFY(upToDate.wait(5000));

        body = release(QStringLiteral("v99.0.0"), false, true);
        checker.check();
        QVERIFY(upToDate.wait(5000));

        body = release(QStringLiteral("v0.0.1"));
        checker.check();
        QVERIFY(upToDate.wait(5000));
        QCOMPARE(upToDate.size(), 4);
        QCOMPARE(available.size(), 0);
    }

    void failuresAreReported_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("hint");
        QTest::newRow("no releases") << 404 << QByteArray(R"({"message":"Not Found"})") << QStringLiteral("release");
        QTest::newRow("rate limit") << 403 << QByteArray(R"({"message":"API rate limit exceeded"})")
                                    << QStringLiteral("later");
        QTest::newRow("server error") << 502 << QByteArray("Bad gateway") << QStringLiteral("GitHub");
        QTest::newRow("garbage") << 200 << QByteArray("<html>not json</html>") << QStringLiteral("unexpected");
        QTest::newRow("no tag") << 200 << QByteArray("{\"html_url\":\"https://example.com\"}")
                                << QStringLiteral("unexpected");
    }

    void failuresAreReported()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, hint);
        MockHttpServer server;
        QVERIFY(server.listen());
        server.setHandler([&](const MockHttpServer::Request &) { return MockHttpServer::Response::json(body, status); });
        QNetworkAccessManager network;
        UpdateChecker checker(&network);
        checker.setApiUrl(server.baseUrl() + QStringLiteral("/latest"));
        QSignalSpy failed(&checker, &UpdateChecker::failed);
        QSignalSpy upToDate(&checker, &UpdateChecker::upToDate);
        checker.check();
        QVERIFY(failed.wait(5000));
        const QString message = failed.first().at(0).toString();
        QVERIFY2(message.contains(hint, Qt::CaseInsensitive), qPrintable(message));
        QCOMPARE(upToDate.size(), 0);
    }

    void unreachableServerFails()
    {
        quint16 port = 0;
        {
            MockHttpServer server;
            QVERIFY(server.listen());
            port = server.port();
        } // closed again: nothing listens there now
        QNetworkAccessManager network;
        UpdateChecker checker(&network);
        checker.setApiUrl(QStringLiteral("http://127.0.0.1:%1/latest").arg(port));
        QSignalSpy failed(&checker, &UpdateChecker::failed);
        checker.check();
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("internet connection")));
    }
};

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
