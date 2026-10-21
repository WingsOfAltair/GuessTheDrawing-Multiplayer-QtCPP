#include "master/master_server.h"

#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QUuid>
#include <QtTest>

#include <algorithm>

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

		const auto sendAbandonment = [&gameServer, &authToken](const QString& sequence) {
			return gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("RoundAbandonment"),
				{{QStringLiteral("requestId"), sequence},
				 {QStringLiteral("authToken"), authToken}, {QStringLiteral("username"), QStringLiteral("scoreuser")}}));
		};
		QVERIFY(sendAbandonment(QStringLiteral("4")));
		QTRY_VERIFY(!findResponse(QStringLiteral("RoundAbandonmentStored"), QStringLiteral("4")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->roundsAbandoned, 1);
		QVERIFY(sendAbandonment(QStringLiteral("4")));
		QTRY_VERIFY(!findResponse(QStringLiteral("Error"), QStringLiteral("4")).isEmpty());
		profile = accountStore.profile(QStringLiteral("scoreuser"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->roundsAbandoned, 1);
	}

	void enforcesServerStaffAndModerationAuthorization()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString databasePath = directory.filePath(QStringLiteral("moderation.sqlite"));
		gtd::AccountStore accountStore(databasePath);
		QString error;
		QVERIFY2(accountStore.initialize(&error), qPrintable(error));
		for (const QString& user : {QStringLiteral("adminuser"), QStringLiteral("moduser"),
			QStringLiteral("regularuser"), QStringLiteral("newmod"), QStringLiteral("victim"),
			QStringLiteral("superuser")})
			QVERIFY2(accountStore.createAccount(user, QStringLiteral("password123"), user, &error), qPrintable(error));
		QVERIFY2(accountStore.setServerRole(QStringLiteral("moderation-room"), QStringLiteral("adminuser"),
			QStringLiteral("admin"), QStringLiteral("adminuser"), &error), qPrintable(error));
		QVERIFY2(accountStore.setServerRole(QStringLiteral("moderation-room"), QStringLiteral("moduser"),
			QStringLiteral("moderator"), QStringLiteral("adminuser"), &error), qPrintable(error));
		const QString seedConnectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
		{
			QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConnectionName);
			seed.setDatabaseName(databasePath);
			QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
			QSqlQuery query(seed);
			query.prepare(QStringLiteral("INSERT INTO master_super_admins(username) VALUES(:username)"));
			query.bindValue(QStringLiteral(":username"), QStringLiteral("superuser"));
			QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
		}
		QSqlDatabase::removeDatabase(seedConnectionName);

		const QString serverSecret = QStringLiteral("moderation-test-server-secret-long-enough");
		const QString sessionSecret = QStringLiteral("moderation-test-session-secret-long-enough");
		gtd::MasterServer master(&accountStore, serverSecret, sessionSecret);
		QVERIFY2(master.start(QHostAddress::LocalHost, 0), qPrintable(QStringLiteral("Could not start master")));

		QTcpSocket gameServer;
		QByteArray serverBuffer;
		QList<QJsonObject> serverResponses;
		connect(&gameServer, &QTcpSocket::readyRead, this, [&] {
			for (const QJsonObject& response : gtd::takeMessages(&serverBuffer, gameServer.readAll()))
				serverResponses.append(response);
		});
		gameServer.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(gameServer.state(), QAbstractSocket::ConnectedState);
		QVERIFY(gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("ServerRegister"),
			{{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("secret"), serverSecret}, {QStringLiteral("rooms"), QJsonArray{}}})));
		auto findServer = [&serverResponses](const QString& type, const QString& requestId) {
			for (const QJsonObject& response : serverResponses) {
				if (response.value(QStringLiteral("type")).toString() == type
					&& response.value(QStringLiteral("requestId")).toString() == requestId)
					return response;
			}
			return QJsonObject{};
		};
		QTRY_VERIFY(!findServer(QStringLiteral("ServerRegisterAck"), QString()).isEmpty());

		QTcpSocket client;
		QByteArray clientBuffer;
		QList<QJsonObject> clientResponses;
		connect(&client, &QTcpSocket::readyRead, this, [&] {
			for (const QJsonObject& response : gtd::takeMessages(&clientBuffer, client.readAll()))
				clientResponses.append(response);
		});
		client.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
		const auto tokenFor = [&sessionSecret](const QString& username) {
			return gtd::createSessionToken(username, username, sessionSecret,
				QDateTime::currentSecsSinceEpoch() + 600);
		};
		auto findClient = [&clientResponses](const QString& type, const QString& requestId) {
			for (const QJsonObject& response : clientResponses) {
				if (response.value(QStringLiteral("type")).toString() == type
					&& response.value(QStringLiteral("requestId")).toString() == requestId)
					return response;
			}
			return QJsonObject{};
		};
		auto sendClient = [&client, &tokenFor](const QString& actor, const QString& type,
			const QString& requestId, QJsonObject fields) {
			fields.insert(QStringLiteral("requestId"), requestId);
			fields.insert(QStringLiteral("authToken"), tokenFor(actor));
			return gtd::sendMessage(&client, gtd::makeMessage(type, fields));
		};

		QVERIFY(sendClient(QStringLiteral("adminuser"), QStringLiteral("StaffRoleRequest"),
			QStringLiteral("role-1"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("newmod")},
			 {QStringLiteral("role"), QStringLiteral("moderator")}}));
		QTRY_VERIFY(!findClient(QStringLiteral("StaffRoleResult"), QStringLiteral("role-1")).isEmpty());
		QCOMPARE(accountStore.serverRole(QStringLiteral("moderation-room"), QStringLiteral("newmod"), &error),
			QStringLiteral("moderator"));

		QVERIFY(sendClient(QStringLiteral("moduser"), QStringLiteral("StaffRoleRequest"),
			QStringLiteral("role-2"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("regularuser")},
			 {QStringLiteral("role"), QStringLiteral("moderator")}}));
		QTRY_VERIFY(!findClient(QStringLiteral("Error"), QStringLiteral("role-2")).isEmpty());

		QVERIFY(sendClient(QStringLiteral("adminuser"), QStringLiteral("ModerationRequest"),
			QStringLiteral("ban-1"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("victim")},
			 {QStringLiteral("action"), QStringLiteral("ban")},
			 {QStringLiteral("reason"), QStringLiteral("Repeated disruption")}}));
		QTRY_VERIFY(!findClient(QStringLiteral("ModerationResult"), QStringLiteral("ban-1")).isEmpty());
		QTRY_VERIFY(!findServer(QStringLiteral("ModerationApplied"), QString()).isEmpty());
		const QJsonObject appliedBan = findServer(QStringLiteral("ModerationApplied"), QString());
		QCOMPARE(appliedBan.value(QStringLiteral("issuedBy")).toString(), QStringLiteral("adminuser"));
		QCOMPARE(appliedBan.value(QStringLiteral("reason")).toString(), QStringLiteral("Repeated disruption"));
		const auto sanctions = accountStore.activeServerSanctions(QStringLiteral("moderation-room"),
			QStringLiteral("victim"), -1, &error);
		QCOMPARE(sanctions.size(), 1);
		QCOMPARE(sanctions.first().kind, QStringLiteral("ban"));

		const QString victimToken = tokenFor(QStringLiteral("victim"));
		QVERIFY(gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("ServerJoinCheck"),
			{{QStringLiteral("requestId"), QStringLiteral("join-1")}, {QStringLiteral("authToken"), victimToken}})));
		QTRY_VERIFY(!findServer(QStringLiteral("ServerJoinCheckResult"), QStringLiteral("join-1")).isEmpty());
		QVERIFY(!findServer(QStringLiteral("ServerJoinCheckResult"), QStringLiteral("join-1"))
			.value(QStringLiteral("ok")).toBool());
		QTcpSocket victimClient;
		QByteArray victimBuffer;
		QList<QJsonObject> victimResponses;
		connect(&victimClient, &QTcpSocket::readyRead, this, [&] {
			for (const QJsonObject& response : gtd::takeMessages(&victimBuffer, victimClient.readAll()))
				victimResponses.append(response);
		});
		victimClient.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(victimClient.state(), QAbstractSocket::ConnectedState);
		QVERIFY(gtd::sendMessage(&victimClient, gtd::makeMessage(QStringLiteral("Login"),
			{{QStringLiteral("requestId"), QStringLiteral("victim-login")},
			 {QStringLiteral("username"), QStringLiteral("victim")},
			 {QStringLiteral("password"), QStringLiteral("password123")}})));
		QTRY_VERIFY(std::any_of(victimResponses.cbegin(), victimResponses.cend(), [](const QJsonObject& response) {
			return response.value(QStringLiteral("type")).toString() == QStringLiteral("AuthResult")
				&& response.value(QStringLiteral("requestId")).toString() == QStringLiteral("victim-login");
		}));

		serverResponses.clear();
		QVERIFY(sendClient(QStringLiteral("adminuser"), QStringLiteral("ModerationRequest"),
			QStringLiteral("unban-1"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("victim")},
			 {QStringLiteral("action"), QStringLiteral("unban")},
			 {QStringLiteral("sanctionId"), appliedBan.value(QStringLiteral("sanction")).toObject()
				.value(QStringLiteral("id"))}}));
		QTRY_VERIFY(!findClient(QStringLiteral("ModerationResult"), QStringLiteral("unban-1")).isEmpty());
		QVERIFY(!findServer(QStringLiteral("ModerationApplied"), QString()).isEmpty());
		QCOMPARE(findServer(QStringLiteral("ModerationApplied"), QString()).value(QStringLiteral("action")).toString(),
			QStringLiteral("unban"));
		QCOMPARE(findServer(QStringLiteral("ModerationApplied"), QString()).value(QStringLiteral("reason")).toString(),
			QStringLiteral("Repeated disruption"));
		QTRY_VERIFY(std::any_of(victimResponses.cbegin(), victimResponses.cend(), [](const QJsonObject& response) {
			return response.value(QStringLiteral("type")).toString() == QStringLiteral("ModerationNotice")
				&& response.value(QStringLiteral("action")).toString() == QStringLiteral("unban");
		}));
		const auto findVictimNotice = [&victimResponses](const QString& action) {
			for (const QJsonObject& response : victimResponses) {
				if (response.value(QStringLiteral("type")).toString() == QStringLiteral("ModerationNotice")
					&& response.value(QStringLiteral("action")).toString() == action)
					return response;
			}
			return QJsonObject{};
		};
		QCOMPARE(findVictimNotice(QStringLiteral("unban")).value(QStringLiteral("reason")).toString(),
			QStringLiteral("Repeated disruption"));
		QVERIFY(accountStore.activeServerSanctions(QStringLiteral("moderation-room"),
			QStringLiteral("victim"), -1, &error).isEmpty());

		serverResponses.clear();
		QVERIFY(sendClient(QStringLiteral("adminuser"), QStringLiteral("ModerationRequest"),
			QStringLiteral("mute-1"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("victim")},
			 {QStringLiteral("action"), QStringLiteral("mute")}, {QStringLiteral("textChat"), true},
			 {QStringLiteral("voiceChat"), false}, {QStringLiteral("reason"), QStringLiteral("Testing mute")},
				 {QStringLiteral("expiresAt"), QDateTime::currentSecsSinceEpoch() + 300}}));
		QTRY_VERIFY(!findClient(QStringLiteral("ModerationResult"), QStringLiteral("mute-1")).isEmpty());
		QTRY_VERIFY(!findServer(QStringLiteral("ModerationApplied"), QString()).isEmpty());
		const QJsonObject appliedMute = findServer(QStringLiteral("ModerationApplied"), QString());
		serverResponses.clear();
		QVERIFY(sendClient(QStringLiteral("adminuser"), QStringLiteral("ModerationRequest"),
			QStringLiteral("unmute-1"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("victim")},
			 {QStringLiteral("action"), QStringLiteral("unmute")},
			 {QStringLiteral("sanctionId"), appliedMute.value(QStringLiteral("sanction")).toObject()
				.value(QStringLiteral("id"))}}));
		QTRY_VERIFY(!findClient(QStringLiteral("ModerationResult"), QStringLiteral("unmute-1")).isEmpty());
		QTRY_VERIFY(findVictimNotice(QStringLiteral("unmute")).value(QStringLiteral("reason")).toString()
			== QStringLiteral("Testing mute"));

		QVERIFY(sendClient(QStringLiteral("superuser"), QStringLiteral("ModerationRequest"),
			QStringLiteral("ban-2"), {{QStringLiteral("serverId"), QStringLiteral("moderation-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("adminuser")},
			 {QStringLiteral("action"), QStringLiteral("ban")},
			 {QStringLiteral("reason"), QStringLiteral("Super-admin review")}}));
		QTRY_VERIFY(!findClient(QStringLiteral("ModerationResult"), QStringLiteral("ban-2")).isEmpty());
		QVERIFY(accountStore.activeServerSanctions(QStringLiteral("moderation-room"),
			QStringLiteral("adminuser"), -1, &error).size() == 1);
	}

	void appliesExternalSuperAdminChangesWithoutRestart()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString databasePath = directory.filePath(QStringLiteral("live-super-admin.sqlite"));
		gtd::AccountStore accountStore(databasePath);
		QString error;
		QVERIFY2(accountStore.initialize(&error), qPrintable(error));
		for (const QString& username : {QStringLiteral("liveadmin"), QStringLiteral("livetarget"),
			QStringLiteral("liveoperator")})
			QVERIFY2(accountStore.createAccount(username, QStringLiteral("password123"), username, &error), qPrintable(error));
		QVERIFY2(accountStore.setServerRole(QStringLiteral("live-room"), QStringLiteral("liveadmin"),
			QStringLiteral("admin"), QStringLiteral("liveadmin"), &error), qPrintable(error));

		const QString serverSecret = QStringLiteral("live-test-server-secret-long-enough");
		const QString sessionSecret = QStringLiteral("live-test-session-secret-long-enough");
		gtd::MasterServer master(&accountStore, serverSecret, sessionSecret);
		QVERIFY(master.start(QHostAddress::LocalHost, 0));
		QTcpSocket gameServer;
		QByteArray serverBuffer;
		QList<QJsonObject> serverMessages;
		connect(&gameServer, &QTcpSocket::readyRead, this, [&] {
			for (const QJsonObject& message : gtd::takeMessages(&serverBuffer, gameServer.readAll()))
				serverMessages.append(message);
		});
		gameServer.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(gameServer.state(), QAbstractSocket::ConnectedState);
		QVERIFY(gtd::sendMessage(&gameServer, gtd::makeMessage(QStringLiteral("ServerRegister"),
			{{QStringLiteral("serverId"), QStringLiteral("live-room")},
			 {QStringLiteral("secret"), serverSecret}, {QStringLiteral("rooms"), QJsonArray{}}})));
		QTRY_VERIFY(std::any_of(serverMessages.cbegin(), serverMessages.cend(), [](const QJsonObject& message) {
			return message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerRegisterAck");
		}));

		const QString seedConnectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
		{
			QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConnectionName);
			seed.setDatabaseName(databasePath);
			QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
			QSqlQuery query(seed);
			query.prepare(QStringLiteral("INSERT INTO master_super_admins(username) VALUES(:username)"));
			query.bindValue(QStringLiteral(":username"), QStringLiteral("liveoperator"));
			QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
			seed.close();
		}
		QSqlDatabase::removeDatabase(seedConnectionName);
		auto hasRoleUpdate = [&serverMessages](bool enabled) {
			return std::any_of(serverMessages.cbegin(), serverMessages.cend(), [enabled](const QJsonObject& message) {
				return message.value(QStringLiteral("type")).toString() == QStringLiteral("StaffRoleChanged")
					&& message.value(QStringLiteral("username")).toString() == QStringLiteral("liveoperator")
					&& message.value(QStringLiteral("masterSuperAdmin")).toBool() == enabled;
			});
		};
		QTRY_VERIFY_WITH_TIMEOUT(hasRoleUpdate(true), 4000);

		QTcpSocket client;
		QByteArray clientBuffer;
		QList<QJsonObject> responses;
		connect(&client, &QTcpSocket::readyRead, this, [&] {
			for (const QJsonObject& message : gtd::takeMessages(&clientBuffer, client.readAll()))
				responses.append(message);
		});
		client.connectToHost(QHostAddress::LocalHost, master.serverPort());
		QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
		const QString token = gtd::createSessionToken(QStringLiteral("liveoperator"),
			QStringLiteral("liveoperator"), sessionSecret, QDateTime::currentSecsSinceEpoch() + 600);
		QVERIFY(gtd::sendMessage(&client, gtd::makeMessage(QStringLiteral("ModerationRequest"),
			{{QStringLiteral("requestId"), QStringLiteral("live-grant-action")},
			 {QStringLiteral("authToken"), token}, {QStringLiteral("serverId"), QStringLiteral("live-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("liveadmin")},
			 {QStringLiteral("action"), QStringLiteral("kick")},
			 {QStringLiteral("reason"), QStringLiteral("Live super-admin test")}})));
		QTRY_VERIFY(std::any_of(responses.cbegin(), responses.cend(), [](const QJsonObject& message) {
			return message.value(QStringLiteral("type")).toString() == QStringLiteral("ModerationResult")
				&& message.value(QStringLiteral("requestId")).toString() == QStringLiteral("live-grant-action");
		}));

		{
			QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConnectionName);
			seed.setDatabaseName(databasePath);
			QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
			QSqlQuery query(seed);
			query.prepare(QStringLiteral("DELETE FROM master_super_admins WHERE username = :username"));
			query.bindValue(QStringLiteral(":username"), QStringLiteral("liveoperator"));
			QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
			seed.close();
		}
		QSqlDatabase::removeDatabase(seedConnectionName);
		QTRY_VERIFY_WITH_TIMEOUT(hasRoleUpdate(false), 4000);
		QVERIFY(gtd::sendMessage(&client, gtd::makeMessage(QStringLiteral("ModerationRequest"),
			{{QStringLiteral("requestId"), QStringLiteral("live-revoked-action")},
			 {QStringLiteral("authToken"), token}, {QStringLiteral("serverId"), QStringLiteral("live-room")},
			 {QStringLiteral("targetUsername"), QStringLiteral("liveadmin")},
			 {QStringLiteral("action"), QStringLiteral("kick")},
			 {QStringLiteral("reason"), QStringLiteral("Revoked super-admin test")}})));
		QTRY_VERIFY(std::any_of(responses.cbegin(), responses.cend(), [](const QJsonObject& message) {
			return message.value(QStringLiteral("type")).toString() == QStringLiteral("Error")
				&& message.value(QStringLiteral("requestId")).toString() == QStringLiteral("live-revoked-action");
		}));
	}
};

QTEST_GUILESS_MAIN(MasterServerTests)
#include "test_master_server.moc"
