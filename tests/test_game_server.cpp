#include "game/game_server.h"

#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class GameServerTests final : public QObject {
	Q_OBJECT

private slots:
	void repliesToPingBeforeJoin()
	{
		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));

		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-ping");
		room.roomName = QStringLiteral("Ping room");
		gtd::GameServer server(room, QStringLiteral("server-ping"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"));
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));

		QTcpSocket probe;
		probe.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(probe.state(), QAbstractSocket::ConnectedState);
		QByteArray buffer;
		QString pongRequestId;
		connect(&probe, &QTcpSocket::readyRead, this, [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&buffer, probe.readAll());
			for (const QJsonObject& message : messages) {
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("Pong"))
					pongRequestId = message.value(QStringLiteral("requestId")).toString();
			}
		});
		QVERIFY(gtd::sendMessage(&probe, gtd::makeMessage(QStringLiteral("Ping"),
			{{QStringLiteral("requestId"), QStringLiteral("room-ping-probe")}})));

		QTRY_COMPARE(pongRequestId, QStringLiteral("room-ping-probe"));
	}

	void profileLeaderboardRanksReachRoster()
	{
		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));

		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-profile-ranks");
		room.roomName = QStringLiteral("Profile ranks room");
		gtd::GameServer server(room, QStringLiteral("server-profile-ranks"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"));
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));
		QTRY_VERIFY(master.hasPendingConnections());

		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		bool profileRequestReceived = false;
		auto processMasterData = [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : messages) {
				const QString type = message.value(QStringLiteral("type")).toString();
				if (type == QStringLiteral("ServerRegister")) {
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (type == QStringLiteral("ProfileRequest")) {
					profileRequestReceived = true;
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ProfileResult"),
						{{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
						 {QStringLiteral("profile"), QJsonObject{
							 {QStringLiteral("username"), QStringLiteral("ranked-player")},
							 {QStringLiteral("displayName"), QStringLiteral("Ranked Player")},
							 {QStringLiteral("guessingScore"), 10},
							 {QStringLiteral("drawingScore"), 10},
							 {QStringLiteral("totalScore"), 20},
							 {QStringLiteral("leaderboardRank"), 1},
							 {QStringLiteral("guessingLeaderboardRank"), 1},
							 {QStringLiteral("drawingLeaderboardRank"), 1},
							 {QStringLiteral("guessingRank"), QStringLiteral("Word Seeker I")},
							 {QStringLiteral("drawingRank"), QStringLiteral("Pencil I")}}}}));
				}
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();

		QTcpSocket player;
		player.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(player.state(), QAbstractSocket::ConnectedState);
		QByteArray playerBuffer;
		QJsonObject latestRosterProfile;
		connect(&player, &QTcpSocket::readyRead, this, [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&playerBuffer, player.readAll());
			for (const QJsonObject& message : messages) {
				if (message.value(QStringLiteral("type")).toString() != QStringLiteral("RoomRoster"))
					continue;
				for (const QJsonValue& value : message.value(QStringLiteral("players")).toArray()) {
					const QJsonObject profile = value.toObject().value(QStringLiteral("profile")).toObject();
					if (profile.value(QStringLiteral("username")).toString() == QStringLiteral("ranked-player"))
						latestRosterProfile = profile;
				}
			}
		});
		const QString token = gtd::createSessionToken(QStringLiteral("ranked-player"),
			QStringLiteral("Ranked Player"), QStringLiteral("session-secret"),
			QDateTime::currentSecsSinceEpoch() + 300);
		gtd::sendMessage(&player, gtd::makeMessage(QStringLiteral("JoinRoom"),
			{{QStringLiteral("roomId"), room.roomId}, {QStringLiteral("authToken"), token}}));

		QTRY_VERIFY(profileRequestReceived);
		QTRY_VERIFY(!latestRosterProfile.isEmpty());
		QTRY_COMPARE(latestRosterProfile.value(QStringLiteral("guessingLeaderboardRank")).toInt(), 1);
		QCOMPARE(latestRosterProfile.value(QStringLiteral("totalScore")).toInt(), 20);
		QCOMPARE(latestRosterProfile.value(QStringLiteral("leaderboardRank")).toInt(), 1);
		QCOMPARE(latestRosterProfile.value(QStringLiteral("guessingLeaderboardRank")).toInt(), 1);
		QCOMPARE(latestRosterProfile.value(QStringLiteral("drawingLeaderboardRank")).toInt(), 1);
		QCOMPARE(latestRosterProfile.value(QStringLiteral("guessingRank")).toString(), QStringLiteral("Word Seeker I"));
		QCOMPARE(latestRosterProfile.value(QStringLiteral("drawingRank")).toString(), QStringLiteral("Pencil I"));
	}

	void reconnectsWhenMasterStartsLater()
	{
		QTcpServer portProbe;
		QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
		const quint16 masterPort = portProbe.serverPort();
		portProbe.close();

		QTcpServer master;
		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-reconnect-test");
		room.roomName = QStringLiteral("Reconnect test room");
		gtd::GameServer server(room, QStringLiteral("server-reconnect-test"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), masterPort, QStringLiteral("server-secret"),
			QStringLiteral("session-secret"));
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));

		QTest::qWait(250);
		QVERIFY(master.listen(QHostAddress::LocalHost, masterPort));
		QTRY_VERIFY(master.hasPendingConnections());

		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		bool receivedRegistration = false;
		auto processMasterData = [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : messages)
				receivedRegistration = receivedRegistration
					|| message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerRegister");
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();
		QTRY_VERIFY(receivedRegistration);
	}

	void advertisesLiveRoomPlayerCount()
	{
		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));

		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-test");
		room.roomName = QStringLiteral("Test room");
		room.maxPlayers = 8;
		gtd::GameServer server(room, QStringLiteral("server-test"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"));
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));

		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		int advertisedPlayers = -1;
		bool forwardedPlayerJoin = false;
		auto processMasterData = [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : messages) {
				const QString type = message.value(QStringLiteral("type")).toString();
				if (type == QStringLiteral("ServerRegister")) {
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (type == QStringLiteral("ServerHeartbeat")) {
					const QJsonArray rooms = message.value(QStringLiteral("rooms")).toArray();
					if (!rooms.isEmpty())
						advertisedPlayers = rooms.first().toObject().value(QStringLiteral("players")).toInt(-1);
				} else if (type == QStringLiteral("ServerEvent")) {
					forwardedPlayerJoin = message.value(QStringLiteral("message")).toString()
						.contains(QStringLiteral("Player joined: Player Test (@player-test)"));
				}
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();

		QTcpSocket player;
		player.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(player.state(), QAbstractSocket::ConnectedState);
		bool joined = false;
		int joinResponseRoomPlayers = -1;
		int joinResponseRosterSize = -1;
		QByteArray playerBuffer;
		connect(&player, &QTcpSocket::readyRead, this, [&] {
			const QList<QJsonObject> messages = gtd::takeMessages(&playerBuffer, player.readAll());
			for (const QJsonObject& message : messages) {
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("JoinResult")) {
					joined = message.value(QStringLiteral("ok")).toBool();
					joinResponseRoomPlayers = message.value(QStringLiteral("room")).toObject()
						.value(QStringLiteral("players")).toInt(-1);
					joinResponseRosterSize = message.value(QStringLiteral("players")).toArray().size();
				}
			}
		});
		const QString token = gtd::createSessionToken(QStringLiteral("player-test"),
			QStringLiteral("Player Test"), QStringLiteral("session-secret"),
			QDateTime::currentSecsSinceEpoch() + 300);
		gtd::sendMessage(&player, gtd::makeMessage(QStringLiteral("JoinRoom"),
			{{QStringLiteral("roomId"), room.roomId}, {QStringLiteral("authToken"), token}}));

		QTRY_VERIFY(joined);
		QCOMPARE(joinResponseRosterSize, 1);
		QCOMPARE(joinResponseRoomPlayers, 1);
		QTRY_VERIFY(forwardedPlayerJoin);
		QTRY_COMPARE(advertisedPlayers, 1);
	}

	void spectatorCanJoinFullRoomAndPromoteWhenSpaceOpens()
	{
		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));

		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-spectator-test");
		room.roomName = QStringLiteral("Spectator test room");
		room.maxPlayers = 2;
		gtd::GameServer server(room, QStringLiteral("server-spectator-test"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"));
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));

		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		int advertisedPlayers = -1;
		int advertisedSpectators = -1;
		int heartbeatCount = 0;
		auto processMasterData = [&] {
			const QList<QJsonObject> incoming = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : incoming) {
				const QString type = message.value(QStringLiteral("type")).toString();
				if (type == QStringLiteral("ServerRegister")) {
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (type == QStringLiteral("ServerHeartbeat")) {
					const QJsonArray rooms = message.value(QStringLiteral("rooms")).toArray();
					if (!rooms.isEmpty()) {
						const QJsonObject roomInfo = rooms.first().toObject();
						advertisedPlayers = roomInfo.value(QStringLiteral("players")).toInt(-1);
						advertisedSpectators = roomInfo.value(QStringLiteral("spectators")).toInt(-1);
						++heartbeatCount;
					}
				}
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();

		QHash<QTcpSocket*, QByteArray> buffers;
		QHash<QTcpSocket*, QList<QJsonObject>> received;
		auto monitor = [this, &buffers, &received](QTcpSocket* socket) {
			connect(socket, &QTcpSocket::readyRead, this, [socket, &buffers, &received] {
				const QList<QJsonObject> incoming = gtd::takeMessages(&buffers[socket], socket->readAll());
				for (const QJsonObject& message : incoming)
					received[socket].append(message);
			});
		};
		auto findMessage = [&received](QTcpSocket* socket, const QString& type, const QString& requestId = QString()) {
			for (const QJsonObject& message : received.value(socket)) {
				if (message.value(QStringLiteral("type")).toString() == type
					&& (requestId.isEmpty() || message.value(QStringLiteral("requestId")).toString() == requestId))
					return message;
			}
			return QJsonObject{};
		};
		auto join = [&](QTcpSocket* socket, const QString& username, bool spectator, const QString& requestId) {
			monitor(socket);
			socket->connectToHost(QHostAddress::LocalHost, server.port());
			QTRY_COMPARE(socket->state(), QAbstractSocket::ConnectedState);
			const QString token = gtd::createSessionToken(username, username, QStringLiteral("session-secret"),
				QDateTime::currentSecsSinceEpoch() + 300);
			gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("JoinRoom"),
				{{QStringLiteral("requestId"), requestId}, {QStringLiteral("roomId"), room.roomId},
				 {QStringLiteral("authToken"), token}, {QStringLiteral("spectator"), spectator}}));
			QTRY_VERIFY(!findMessage(socket, QStringLiteral("JoinResult"), requestId).isEmpty());
		};
		auto rosterHas = [&received](QTcpSocket* socket, int playerCount, const QString& username = QString(),
									bool spectator = false, int spectatorCount = -1) {
			for (const QJsonObject& message : received.value(socket)) {
				const QJsonObject roomInfo = message.value(QStringLiteral("room")).toObject();
				if (message.value(QStringLiteral("type")).toString() != QStringLiteral("RoomRoster")
					|| roomInfo.value(QStringLiteral("players")).toInt(-1) != playerCount
					|| (spectatorCount >= 0 && roomInfo.value(QStringLiteral("spectators")).toInt(-1)
						!= spectatorCount))
					continue;
				if (username.isEmpty())
					return true;
				for (const QJsonValue& value : message.value(QStringLiteral("players")).toArray()) {
					const QJsonObject entry = value.toObject();
					if (entry.value(QStringLiteral("profile")).toObject().value(QStringLiteral("username")).toString()
						== username && entry.value(QStringLiteral("spectator")).toBool() == spectator)
						return true;
				}
			}
			return false;
		};

		QTcpSocket firstPlayer;
		QTcpSocket secondPlayer;
		QTcpSocket spectator;
		join(&firstPlayer, QStringLiteral("first-player"), false, QStringLiteral("join-first"));
		QVERIFY(findMessage(&firstPlayer, QStringLiteral("JoinResult"), QStringLiteral("join-first"))
			.value(QStringLiteral("ok")).toBool());
		join(&secondPlayer, QStringLiteral("second-player"), false, QStringLiteral("join-second"));
		QVERIFY(findMessage(&secondPlayer, QStringLiteral("JoinResult"), QStringLiteral("join-second"))
			.value(QStringLiteral("ok")).toBool());
		QTRY_COMPARE(advertisedPlayers, 2);

		const int heartbeatsBeforeSpectator = heartbeatCount;
		join(&spectator, QStringLiteral("room-spectator"), true, QStringLiteral("join-spectator"));
		const QJsonObject spectatorJoin = findMessage(&spectator, QStringLiteral("JoinResult"),
			QStringLiteral("join-spectator"));
		QVERIFY(spectatorJoin.value(QStringLiteral("ok")).toBool());
		QVERIFY(spectatorJoin.value(QStringLiteral("spectator")).toBool());
		QCOMPARE(spectatorJoin.value(QStringLiteral("room")).toObject()
			.value(QStringLiteral("players")).toInt(), 2);
		QCOMPARE(spectatorJoin.value(QStringLiteral("room")).toObject()
			.value(QStringLiteral("spectators")).toInt(), 1);
		QCOMPARE(spectatorJoin.value(QStringLiteral("players")).toArray().size(), 3);
		QTRY_VERIFY(heartbeatCount > heartbeatsBeforeSpectator);
		QCOMPARE(advertisedPlayers, 2);
		QCOMPARE(advertisedSpectators, 1);
		QTRY_VERIFY(rosterHas(&spectator, 2, QString(), false, 1));
		auto queueStatusMatches = [&received](QTcpSocket* socket, int position, int ahead, int total) {
			for (const QJsonObject& message : received.value(socket)) {
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("QueueStatus")
					&& message.value(QStringLiteral("position")).toInt() == position
					&& message.value(QStringLiteral("ahead")).toInt() == ahead
					&& message.value(QStringLiteral("total")).toInt() == total)
					return true;
			}
			return false;
		};

		gtd::sendMessage(&spectator, gtd::makeMessage(QStringLiteral("Guess"),
			{{QStringLiteral("requestId"), QStringLiteral("spectator-guess")},
			 {QStringLiteral("text"), QStringLiteral("test")}}));
		QTRY_VERIFY(!findMessage(&spectator, QStringLiteral("Error"), QStringLiteral("spectator-guess")).isEmpty());

		gtd::sendMessage(&spectator, gtd::makeMessage(QStringLiteral("PromoteToPlayer"),
			{{QStringLiteral("requestId"), QStringLiteral("promotion-full")}}));
		QTRY_VERIFY(!findMessage(&spectator, QStringLiteral("RoleChangeResult"),
			QStringLiteral("promotion-full")).isEmpty());
		QVERIFY(findMessage(&spectator, QStringLiteral("RoleChangeResult"),
			QStringLiteral("promotion-full")).value(QStringLiteral("ok")).toBool());
		QVERIFY(findMessage(&spectator, QStringLiteral("RoleChangeResult"),
			QStringLiteral("promotion-full")).value(QStringLiteral("queued")).toBool());
		QVERIFY(queueStatusMatches(&spectator, 1, 0, 1));

		QTcpSocket waitingPlayer;
		join(&waitingPlayer, QStringLiteral("waiting-player"), false, QStringLiteral("join-waiting"));
		const QJsonObject waitingJoin = findMessage(&waitingPlayer, QStringLiteral("JoinResult"),
			QStringLiteral("join-waiting"));
		QVERIFY(waitingJoin.value(QStringLiteral("ok")).toBool());
		QVERIFY(waitingJoin.value(QStringLiteral("queuedForPlayer")).toBool());
		QVERIFY(queueStatusMatches(&waitingPlayer, 2, 1, 2));
		QVERIFY(queueStatusMatches(&spectator, 1, 0, 2));
		gtd::sendMessage(&waitingPlayer, gtd::makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), QStringLiteral("cancel-wait")},
			 {QStringLiteral("spectator"), true}}));
		QTRY_VERIFY(!findMessage(&waitingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("cancel-wait")).isEmpty());
		QVERIFY(findMessage(&waitingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("cancel-wait")).value(QStringLiteral("ok")).toBool());
		QTRY_VERIFY(queueStatusMatches(&waitingPlayer, 0, 0, 1));
		QTRY_VERIFY(queueStatusMatches(&spectator, 1, 0, 1));

		gtd::sendMessage(&firstPlayer, gtd::makeMessage(QStringLiteral("LeaveRoom")));
		QTRY_COMPARE(firstPlayer.state(), QAbstractSocket::UnconnectedState);
		QTRY_VERIFY(rosterHas(&spectator, 2, QStringLiteral("room-spectator"), false, 1));
		QTRY_VERIFY(!findMessage(&spectator, QStringLiteral("RoleChanged")).isEmpty());
		QTRY_VERIFY(rosterHas(&spectator, 2, QStringLiteral("room-spectator"), false, 1));
		QTRY_COMPARE(advertisedPlayers, 2);
		QCOMPARE(advertisedSpectators, 1);
		gtd::sendMessage(&secondPlayer, gtd::makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), QStringLiteral("demotion")},
			 {QStringLiteral("spectator"), true}}));
		QTRY_VERIFY(!findMessage(&secondPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("demotion")).isEmpty());
		QVERIFY(findMessage(&secondPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("demotion")).value(QStringLiteral("ok")).toBool());
		QTRY_VERIFY(rosterHas(&spectator, 1, QStringLiteral("second-player"), true, 2));
		QTRY_COMPARE(advertisedPlayers, 1);
		QCOMPARE(advertisedSpectators, 2);
		gtd::sendMessage(&secondPlayer, gtd::makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), QStringLiteral("rejoin-player")},
			 {QStringLiteral("spectator"), false}}));
		QTRY_VERIFY(!findMessage(&secondPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("rejoin-player")).isEmpty());
		QVERIFY(findMessage(&secondPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("rejoin-player")).value(QStringLiteral("ok")).toBool());
		QTRY_VERIFY(rosterHas(&spectator, 2, QStringLiteral("second-player"), false, 1));
		QTRY_COMPARE(advertisedPlayers, 2);
		QCOMPARE(advertisedSpectators, 1);

		gtd::sendMessage(&secondPlayer, gtd::makeMessage(QStringLiteral("LeaveRoom")));
		QTRY_COMPARE(secondPlayer.state(), QAbstractSocket::UnconnectedState);
		gtd::sendMessage(&spectator, gtd::makeMessage(QStringLiteral("LeaveRoom")));
		QTRY_COMPARE(spectator.state(), QAbstractSocket::UnconnectedState);
		gtd::sendMessage(&waitingPlayer, gtd::makeMessage(QStringLiteral("LeaveRoom")));
		QTRY_COMPARE(waitingPlayer.state(), QAbstractSocket::UnconnectedState);
		QTest::qWait(50);
	}
};

QTEST_GUILESS_MAIN(GameServerTests)
#include "test_game_server.moc"
