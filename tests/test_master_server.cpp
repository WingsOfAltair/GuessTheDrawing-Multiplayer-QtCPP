#include "master/master_server.h"

#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QtTest>

class MasterServerTests final : public QObject {
	Q_OBJECT

private slots:
	void rejectsReplayedAndImpossibleScoreUpdates()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		gtd::AccountStore accountStore(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(accountStore.initialize(&error), qPrintable(error));
		QVERIFY2(accountStore.createAccount(QStringLiteral("scoreuser"), QStringLiteral("password123"),
			QStringLiteral("Score User"), &error), qPrintable(error));

		const QString serverSecret = QStringLiteral("master-test-server-secret-long-enough");
		const QString sessionSecret = QStringLiteral("master-test-session-secret-long-enough");
		gtd::MasterServer master(&accountStore, serverSecret, sessionSecret);
		QVERIFY2(master.start(QHostAddress::LocalHost, 0), qPrintable(QStringLiteral("Could not start master")));

		QTcpSocket gameServer;
		QByteArray responseBuffer;
		QList<QJsonObject> responses;
		connect(&gameServer, &QTcpSocket::readyRead, this, [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&responseBuffer, gameServer.readAll());
			for (const QJsonObject& message : messages)
				responses.append(message);
		});
		gameServer.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(gameServer.state(), QAbstractSocket::ConnectedState);
		QVERIFY(gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("ServerRegister"),
			{{QStringLiteral("serverId"), QStringLiteral("anti-cheat-test")},
			 {QStringLiteral("secret"), serverSecret}, {QStringLiteral("rooms"), QJsonArray{}}})));

		auto findResponse = [&responses](const QString& type, const QString& requestId) {
			for (const QJsonObject& message : responses) {
				if (message.value(QStringLiteral("type")).toString() == type
					&& message.value(QStringLiteral("requestId")).toString() == requestId)
					return message;
			}
			return QJsonObject{};
		};
		QTRY_VERIFY(!findResponse(QStringLiteral("ServerRegisterAck"), QString()).isEmpty());

		auto sendScore = [&gameServer](const QString& sequence, int points, bool drawingPoints) {
			return gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("ScoreUpdate"),
				{{QStringLiteral("requestId"), sequence}, {QStringLiteral("username"), QStringLiteral("scoreuser")},
				 {QStringLiteral("points"), points}, {QStringLiteral("drawingPoints"), drawingPoints}}));
		};
		QVERIFY(sendScore(QStringLiteral("1"), 10, false));
		QTRY_VERIFY(!findResponse(QStringLiteral("ScoreStored"), QStringLiteral("1")).isEmpty());
		auto profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->guessingScore, 10);

		QVERIFY(sendScore(QStringLiteral("1"), 10, false));
		QTRY_VERIFY(!findResponse(QStringLiteral("Error"), QStringLiteral("1")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->guessingScore, 10);

		QVERIFY(sendScore(QStringLiteral("2"), 10000, false));
		QTRY_VERIFY(!findResponse(QStringLiteral("Error"), QStringLiteral("2")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->guessingScore, 10);

		QVERIFY(sendScore(QStringLiteral("2"), 7, false));
		QTRY_VERIFY(!findResponse(QStringLiteral("ScoreStored"), QStringLiteral("2")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->guessingScore, 17);

		const QString authToken = gtd::createSessionToken(QStringLiteral("scoreuser"),
			QStringLiteral("Score User"), sessionSecret, QDateTime::currentSecsSinceEpoch() + 60);
		auto sendRoundResult = [&gameServer, &authToken](const QString& sequence, const QJsonValue& won) {
			return gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("RoundResult"),
				{{QStringLiteral("requestId"), sequence},
				 {QStringLiteral("authToken"), authToken}, {QStringLiteral("username"), QStringLiteral("scoreuser")},
				 {QStringLiteral("roundWon"), won}}));
		};
		QVERIFY(sendRoundResult(QStringLiteral("3"), QStringLiteral("true")));
		QTRY_VERIFY(!findResponse(QStringLiteral("Error"), QStringLiteral("3")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->roundsLost, 0);
		QVERIFY(sendRoundResult(QStringLiteral("3"), true));
		QTRY_VERIFY(!findResponse(QStringLiteral("RoundResultStored"), QStringLiteral("3")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->roundsWon, 1);
	}
};

QTEST_GUILESS_MAIN(MasterServerTests)
#include "test_master_server.moc"
