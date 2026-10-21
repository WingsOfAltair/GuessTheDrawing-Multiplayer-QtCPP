#include "game/game_server.h"

#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

void respondToJoinCheck(QTcpSocket* socket, const QJsonObject& request)
{
	QString username;
	QString displayName;
	const bool valid = gtd::verifySessionToken(request.value(QStringLiteral("authToken")).toString(),
		QStringLiteral("session-secret"), &username, -1, &displayName);
	gtd::PlayerProfile profile;
	profile.username = username;
	profile.displayName = displayName;
	gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("ServerJoinCheckResult"),
		{{QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
		 {QStringLiteral("ok"), valid}, {QStringLiteral("username"), username},
		 {QStringLiteral("profile"), profile.toJson()}, {QStringLiteral("sanctions"), QJsonArray{}},
		 {QStringLiteral("playerMutes"), QJsonArray{}}}));
}

void sendRoomChat(QTcpSocket* socket, const QString& requestId, const QString& text)
{
	gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("RoomChat"),
		{{QStringLiteral("requestId"), requestId}, {QStringLiteral("text"), text}}));
}

void sendVoiceFrame(QTcpSocket* socket)
{
	const QByteArray audio(640, '\x2a');
	gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("VoiceFrame"),
		{{QStringLiteral("sampleRate"), 16000}, {QStringLiteral("audio"), QString::fromLatin1(audio.toBase64())}}));
}

}

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

	void preRoundIntermissionAllowsLeavingAndSpectatingWithoutLosses()
	{
		QTemporaryDir dictionaryDirectory;
		QVERIFY(dictionaryDirectory.isValid());
		const QString dictionaryPath = dictionaryDirectory.filePath(QStringLiteral("words.txt"));
		QFile dictionary(dictionaryPath);
		QVERIFY(dictionary.open(QIODevice::WriteOnly | QIODevice::Text));
		QCOMPARE(dictionary.write("intermissionword\n"), qint64(17));
		dictionary.close();

		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));
		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-intermission");
		room.roomName = QStringLiteral("Intermission room");
		gtd::GameServer server(room, QStringLiteral("server-intermission"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"), dictionaryPath, 5);
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));
		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		QList<QJsonObject> masterMessages;
		bool registered = false;
		auto processMasterData = [&] {
			const QList<QJsonObject> incoming = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : incoming) {
				masterMessages.append(message);
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerRegister")) {
					registered = true;
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerJoinCheck")) {
					respondToJoinCheck(masterSocket, message);
				}
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();
		QTRY_VERIFY(registered);

		QHash<QTcpSocket*, QByteArray> buffers;
		QHash<QTcpSocket*, QList<QJsonObject>> received;
		auto monitor = [this, &buffers, &received](QTcpSocket* socket) {
			connect(socket, &QTcpSocket::readyRead, this, [socket, &buffers, &received] {
				const QList<QJsonObject> incoming = gtd::takeMessages(&buffers[socket], socket->readAll());
				for (const QJsonObject& message : incoming)
					received[socket].append(message);
			});
		};
		auto findMessage = [&received](QTcpSocket* socket, const QString& type,
									 const QString& requestId = QString()) {
			for (const QJsonObject& message : received.value(socket)) {
				if (message.value(QStringLiteral("type")).toString() == type
					&& (requestId.isEmpty() || message.value(QStringLiteral("requestId")).toString() == requestId))
					return message;
			}
			return QJsonObject{};
		};
		auto join = [&](QTcpSocket* socket, const QString& username, const QString& requestId) {
			monitor(socket);
			socket->connectToHost(QHostAddress::LocalHost, server.port());
			QTRY_COMPARE(socket->state(), QAbstractSocket::ConnectedState);
			const QString token = gtd::createSessionToken(username, username, QStringLiteral("session-secret"),
				QDateTime::currentSecsSinceEpoch() + 300);
			QVERIFY(gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("JoinRoom"),
				{{QStringLiteral("requestId"), requestId}, {QStringLiteral("roomId"), room.roomId},
				 {QStringLiteral("authToken"), token}})));
			QTRY_VERIFY(!findMessage(socket, QStringLiteral("JoinResult"), requestId).isEmpty());
			QVERIFY(findMessage(socket, QStringLiteral("JoinResult"), requestId)
				.value(QStringLiteral("ok")).toBool());
		};

		QTcpSocket leavingPlayer;
		QTcpSocket spectatingPlayer;
		QTcpSocket activePlayerOne;
		QTcpSocket activePlayerTwo;
		join(&leavingPlayer, QStringLiteral("intermission-leaving"), QStringLiteral("join-leaving"));
		QElapsedTimer intermissionElapsed;
		intermissionElapsed.start();
		join(&spectatingPlayer, QStringLiteral("intermission-spectating"), QStringLiteral("join-spectating"));
		QTRY_VERIFY(!findMessage(&leavingPlayer, QStringLiteral("RoundStarting")).isEmpty());
		join(&activePlayerOne, QStringLiteral("intermission-active-one"), QStringLiteral("join-active-one"));
		join(&activePlayerTwo, QStringLiteral("intermission-active-two"), QStringLiteral("join-active-two"));

		QVERIFY(gtd::sendMessage(&leavingPlayer, gtd::makeMessage(QStringLiteral("LeaveRoom"))));
		QTRY_COMPARE(leavingPlayer.state(), QAbstractSocket::UnconnectedState);
		QVERIFY(gtd::sendMessage(&spectatingPlayer, gtd::makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), QStringLiteral("become-spectator")},
			 {QStringLiteral("spectator"), true}})));
		QTRY_VERIFY(!findMessage(&spectatingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("become-spectator")).isEmpty());
		QVERIFY(findMessage(&spectatingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("become-spectator")).value(QStringLiteral("ok")).toBool());
		QTRY_VERIFY(!findMessage(&activePlayerOne, QStringLiteral("IntermissionCountdown")).isEmpty());
		QVERIFY(findMessage(&activePlayerOne, QStringLiteral("RoundStarted")).isEmpty());
		QTRY_VERIFY(!findMessage(&activePlayerOne, QStringLiteral("RoundStarted")).isEmpty());
		QVERIFY(intermissionElapsed.elapsed() >= 4000);

		const QString drawerUsername = findMessage(&activePlayerOne, QStringLiteral("RoundStarted"))
			.value(QStringLiteral("drawerUsername")).toString();
		QTcpSocket* drawer = drawerUsername == QStringLiteral("intermission-active-one")
			? &activePlayerOne : &activePlayerTwo;
		QTcpSocket* guesser = drawer == &activePlayerOne ? &activePlayerTwo : &activePlayerOne;
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("WordAssigned")).isEmpty());
		QVERIFY(gtd::sendMessage(guesser, gtd::makeMessage(QStringLiteral("Guess"),
			{{QStringLiteral("text"), QStringLiteral("intermissionword")}})));
		QTRY_VERIFY(!findMessage(guesser, QStringLiteral("RoundEnded")).isEmpty());
		QTRY_VERIFY(std::count_if(masterMessages.cbegin(), masterMessages.cend(), [](const QJsonObject& message) {
			return message.value(QStringLiteral("type")).toString() == QStringLiteral("RoundResult");
		}) == 2);

		QStringList resultUsernames;
		for (const QJsonObject& message : masterMessages) {
			if (message.value(QStringLiteral("type")).toString() == QStringLiteral("RoundResult"))
				resultUsernames.append(message.value(QStringLiteral("username")).toString());
		}
		QStringList expectedUsernames{QStringLiteral("intermission-active-one"), QStringLiteral("intermission-active-two")};
		resultUsernames.sort();
		expectedUsernames.sort();
		QCOMPARE(resultUsernames, expectedUsernames);
		QVERIFY(std::none_of(masterMessages.cbegin(), masterMessages.cend(), [](const QJsonObject& message) {
			return message.value(QStringLiteral("type")).toString() == QStringLiteral("RoundAbandonment");
		}));
	}

	void activeRoundDepartureCountsAsAbandonmentInsteadOfLoss()
	{
		QTemporaryDir dictionaryDirectory;
		QVERIFY(dictionaryDirectory.isValid());
		const QString dictionaryPath = dictionaryDirectory.filePath(QStringLiteral("words.txt"));
		QFile dictionary(dictionaryPath);
		QVERIFY(dictionary.open(QIODevice::WriteOnly | QIODevice::Text));
		QCOMPARE(dictionary.write("quitword\n"), qint64(9));
		dictionary.close();

		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));
		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-abandonment");
		room.roomName = QStringLiteral("Abandonment room");
		gtd::GameServer server(room, QStringLiteral("server-abandonment"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"), dictionaryPath, 5);
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));
		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		QList<QJsonObject> masterMessages;
		bool registered = false;
		auto processMasterData = [&] {
			const QList<QJsonObject> incoming = gtd::takeMessages(&masterBuffer, masterSocket->readAll());
			for (const QJsonObject& message : incoming) {
				masterMessages.append(message);
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerRegister")) {
					registered = true;
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerJoinCheck")) {
					respondToJoinCheck(masterSocket, message);
				}
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();
		QTRY_VERIFY(registered);

		QHash<QTcpSocket*, QByteArray> buffers;
		QHash<QTcpSocket*, QList<QJsonObject>> received;
		auto monitor = [this, &buffers, &received](QTcpSocket* socket) {
			connect(socket, &QTcpSocket::readyRead, this, [socket, &buffers, &received] {
				const QList<QJsonObject> incoming = gtd::takeMessages(&buffers[socket], socket->readAll());
				for (const QJsonObject& message : incoming)
					received[socket].append(message);
			});
		};
		auto findMessage = [&received](QTcpSocket* socket, const QString& type,
									 const QString& requestId = QString()) {
			for (const QJsonObject& message : received.value(socket)) {
				if (message.value(QStringLiteral("type")).toString() == type
					&& (requestId.isEmpty() || message.value(QStringLiteral("requestId")).toString() == requestId))
					return message;
			}
			return QJsonObject{};
		};
		auto join = [&](QTcpSocket* socket, const QString& username) {
			monitor(socket);
			socket->connectToHost(QHostAddress::LocalHost, server.port());
			QTRY_COMPARE(socket->state(), QAbstractSocket::ConnectedState);
			const QString token = gtd::createSessionToken(username, username, QStringLiteral("session-secret"),
				QDateTime::currentSecsSinceEpoch() + 300);
			QVERIFY(gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("JoinRoom"),
				{{QStringLiteral("roomId"), room.roomId}, {QStringLiteral("authToken"), token}})));
			QTRY_VERIFY(!findMessage(socket, QStringLiteral("JoinResult")).isEmpty());
			QVERIFY(findMessage(socket, QStringLiteral("JoinResult")).value(QStringLiteral("ok")).toBool());
		};

		QTcpSocket firstPlayer;
		QTcpSocket secondPlayer;
		QTcpSocket thirdPlayer;
		QTcpSocket fourthPlayer;
		const QStringList usernames{QStringLiteral("abandon-first"), QStringLiteral("abandon-second"),
		QStringLiteral("abandon-third"), QStringLiteral("abandon-fourth")};
		join(&firstPlayer, usernames.at(0));
		join(&secondPlayer, usernames.at(1));
		join(&thirdPlayer, usernames.at(2));
		join(&fourthPlayer, usernames.at(3));
		QTRY_VERIFY(!findMessage(&firstPlayer, QStringLiteral("RoundStarted")).isEmpty());
		const QString drawerUsername = findMessage(&firstPlayer, QStringLiteral("RoundStarted"))
			.value(QStringLiteral("drawerUsername")).toString();
		QHash<QString, QTcpSocket*> socketsByUsername{
			{usernames.at(0), &firstPlayer}, {usernames.at(1), &secondPlayer},
			{usernames.at(2), &thirdPlayer}, {usernames.at(3), &fourthPlayer}};
		QStringList nonDrawerUsernames;
		for (const QString& username : usernames) {
			if (username != drawerUsername)
				nonDrawerUsernames.append(username);
		}
		const QString abandonUsername = nonDrawerUsernames.at(0);
		const QString spectatingUsername = nonDrawerUsernames.at(1);
		const QString guesserUsername = nonDrawerUsernames.at(2);
		QTcpSocket* abandoner = socketsByUsername.value(abandonUsername);
		QTcpSocket* spectatingPlayer = socketsByUsername.value(spectatingUsername);
		QTcpSocket* guesser = socketsByUsername.value(guesserUsername);
		QVERIFY(abandoner);
		QVERIFY(spectatingPlayer);
		QVERIFY(guesser);
		QVERIFY(gtd::sendMessage(abandoner, gtd::makeMessage(QStringLiteral("LeaveRoom"))));
		QTRY_COMPARE(abandoner->state(), QAbstractSocket::UnconnectedState);
		QVERIFY(gtd::sendMessage(spectatingPlayer, gtd::makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), QStringLiteral("active-round-spectate")},
			 {QStringLiteral("spectator"), true}})));
		QTRY_VERIFY(!findMessage(spectatingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("active-round-spectate")).isEmpty());
		QVERIFY(findMessage(spectatingPlayer, QStringLiteral("RoleChangeResult"),
			QStringLiteral("active-round-spectate")).value(QStringLiteral("ok")).toBool());
		QTRY_VERIFY(std::count_if(masterMessages.cbegin(), masterMessages.cend(),
			[](const QJsonObject& message) {
				return message.value(QStringLiteral("type")).toString() == QStringLiteral("RoundAbandonment");
			}) == 2);
		QVERIFY(gtd::sendMessage(guesser, gtd::makeMessage(QStringLiteral("Guess"),
			{{QStringLiteral("text"), QStringLiteral("quitword")}})));
		QTRY_VERIFY(!findMessage(guesser, QStringLiteral("RoundEnded")).isEmpty());
		QTRY_VERIFY(std::count_if(masterMessages.cbegin(), masterMessages.cend(),
			[](const QJsonObject& message) {
				return message.value(QStringLiteral("type")).toString() == QStringLiteral("RoundResult");
			}) == 2);

		QStringList abandonedUsernames;
		QStringList resultUsernames;
		for (const QJsonObject& message : masterMessages) {
			const QString type = message.value(QStringLiteral("type")).toString();
			if (type == QStringLiteral("RoundAbandonment"))
				abandonedUsernames.append(message.value(QStringLiteral("username")).toString());
			else if (type == QStringLiteral("RoundResult"))
				resultUsernames.append(message.value(QStringLiteral("username")).toString());
		}
		QStringList expectedAbandonedUsernames{abandonUsername, spectatingUsername};
		QStringList expectedResultUsernames = usernames;
		expectedResultUsernames.removeAll(abandonUsername);
		expectedResultUsernames.removeAll(spectatingUsername);
		abandonedUsernames.sort();
		expectedAbandonedUsernames.sort();
		resultUsernames.sort();
		expectedResultUsernames.sort();
		QCOMPARE(abandonedUsernames, expectedAbandonedUsernames);
		QCOMPARE(resultUsernames, expectedResultUsernames);
	}

	void relaysRoomChatAndVoiceWhileDrawerIsReceiveOnly()
	{
		QTemporaryDir dictionaryDirectory;
		QVERIFY(dictionaryDirectory.isValid());
		const QString dictionaryPath = dictionaryDirectory.filePath(QStringLiteral("words.txt"));
		QFile dictionary(dictionaryPath);
		QVERIFY(dictionary.open(QIODevice::WriteOnly | QIODevice::Text));
		QCOMPARE(dictionary.write("customword\n"), qint64(11));
		dictionary.close();

		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));

		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-chat-voice");
		room.roomName = QStringLiteral("Chat and voice room");
		room.maxPlayers = 2;
		gtd::GameServer server(room, QStringLiteral("server-chat-voice"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"), dictionaryPath, 0);
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));
		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		auto processMasterData = [&] {
			for (const QJsonObject& message : gtd::takeMessages(&masterBuffer, masterSocket->readAll())) {
				const QString type = message.value(QStringLiteral("type")).toString();
				if (type == QStringLiteral("ServerRegister"))
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				else if (type == QStringLiteral("ServerJoinCheck"))
					respondToJoinCheck(masterSocket, message);
			}
		};
		connect(masterSocket, &QTcpSocket::readyRead, this, processMasterData);
		if (masterSocket->bytesAvailable() > 0)
			processMasterData();

		QHash<QTcpSocket*, QByteArray> buffers;
		QHash<QTcpSocket*, QList<QJsonObject>> received;
		auto monitor = [this, &buffers, &received](QTcpSocket* socket) {
			connect(socket, &QTcpSocket::readyRead, this, [socket, &buffers, &received] {
				const QList<QJsonObject> messages = gtd::takeMessages(&buffers[socket], socket->readAll());
				for (const QJsonObject& message : messages)
					received[socket].append(message);
			});
		};
		auto findMessage = [&received](QTcpSocket* socket, const QString& type, const QString& value = QString()) {
			for (const QJsonObject& message : received.value(socket)) {
				if (message.value(QStringLiteral("type")).toString() == type
					&& (value.isEmpty() || message.value(QStringLiteral("text")).toString() == value
						|| message.value(QStringLiteral("requestId")).toString() == value))
					return message;
			}
			return QJsonObject{};
		};
		auto join = [&](QTcpSocket* socket, const QString& username, const QString& requestId) {
			monitor(socket);
			socket->connectToHost(QHostAddress::LocalHost, server.port());
			QTRY_COMPARE(socket->state(), QAbstractSocket::ConnectedState);
			const QString token = gtd::createSessionToken(username, username, QStringLiteral("session-secret"),
				QDateTime::currentSecsSinceEpoch() + 300);
			QVERIFY(gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("JoinRoom"),
				{{QStringLiteral("requestId"), requestId}, {QStringLiteral("roomId"), room.roomId},
				 {QStringLiteral("authToken"), token}})));
			QTRY_VERIFY(!findMessage(socket, QStringLiteral("JoinResult"), requestId).isEmpty());
			QVERIFY(findMessage(socket, QStringLiteral("JoinResult"), requestId)
				.value(QStringLiteral("ok")).toBool());
		};

		QTcpSocket firstPlayer;
		QTcpSocket secondPlayer;
		join(&firstPlayer, QStringLiteral("voice-first"), QStringLiteral("join-first"));
		join(&secondPlayer, QStringLiteral("voice-second"), QStringLiteral("join-second"));
		QTRY_VERIFY(!findMessage(&firstPlayer, QStringLiteral("RoundStarted")).isEmpty());
		const QString drawerUsername = findMessage(&firstPlayer, QStringLiteral("RoundStarted"))
			.value(QStringLiteral("drawerUsername")).toString();
		QVERIFY(drawerUsername == QStringLiteral("voice-first") || drawerUsername == QStringLiteral("voice-second"));
		QTcpSocket* drawer = drawerUsername == QStringLiteral("voice-first") ? &firstPlayer : &secondPlayer;
		QTcpSocket* listener = drawer == &firstPlayer ? &secondPlayer : &firstPlayer;
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("WordAssigned")).isEmpty());
		QCOMPARE(findMessage(drawer, QStringLiteral("WordAssigned"))
			.value(QStringLiteral("word")).toString(), QStringLiteral("customword"));

		QVERIFY(gtd::sendMessage(drawer, gtd::makeMessage(QStringLiteral("RoomChat"),
			{{QStringLiteral("requestId"), QStringLiteral("drawer-chat")},
			 {QStringLiteral("text"), QStringLiteral("not allowed")}})));
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("Error"), QStringLiteral("drawer-chat")).isEmpty());
		QVERIFY(findMessage(listener, QStringLiteral("ChatMessage"), QStringLiteral("not allowed")).isEmpty());

		QVERIFY(gtd::sendMessage(listener, gtd::makeMessage(QStringLiteral("RoomChat"),
			{{QStringLiteral("requestId"), QStringLiteral("room-chat")},
			 {QStringLiteral("text"), QStringLiteral("hello room")}})));
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("ChatMessage"), QStringLiteral("hello room")).isEmpty());
		QVERIFY(findMessage(drawer, QStringLiteral("ChatMessage"), QStringLiteral("hello room"))
			.value(QStringLiteral("roomChat")).toBool());

		const QString oversizedChat(301, QLatin1Char('x'));
		QVERIFY(gtd::sendMessage(listener, gtd::makeMessage(QStringLiteral("RoomChat"),
			{{QStringLiteral("requestId"), QStringLiteral("oversized-chat")},
			 {QStringLiteral("text"), oversizedChat}})));
		QVERIFY(gtd::sendMessage(listener, gtd::makeMessage(QStringLiteral("RoomChat"),
			{{QStringLiteral("requestId"), QStringLiteral("valid-after-oversized")},
			 {QStringLiteral("text"), QStringLiteral("valid after oversized input")}})));
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("ChatMessage"),
			QStringLiteral("valid after oversized input")).isEmpty());
		QVERIFY(findMessage(drawer, QStringLiteral("ChatMessage"), oversizedChat).isEmpty());

		for (int index = 0; index < 10; ++index) {
			QVERIFY(gtd::sendMessage(listener, gtd::makeMessage(QStringLiteral("RoomChat"),
				{{QStringLiteral("text"), QStringLiteral("flood-%1").arg(index)}})));
		}
		auto hasFloodMessage = [&received, drawer] {
			for (const QJsonObject& message : received.value(drawer)) {
				if (message.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("flood-")))
					return true;
			}
			return false;
		};
		QTRY_VERIFY(hasFloodMessage());
		QTest::qWait(50);
		int floodMessages = 0;
		for (const QJsonObject& message : received.value(drawer)) {
			if (message.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("flood-")))
				++floodMessages;
		}
		QVERIFY(floodMessages <= 8);

		const QByteArray audio(640, '\x2a');
		QVERIFY(gtd::sendMessage(listener, gtd::makeMessage(QStringLiteral("VoiceFrame"),
			{{QStringLiteral("sampleRate"), 16000},
			 {QStringLiteral("audio"), QString::fromLatin1(audio.toBase64())}})));
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("VoiceFrame")).isEmpty());
		QCOMPARE(findMessage(drawer, QStringLiteral("VoiceFrame"))
			.value(QStringLiteral("username")).toString(),
			drawerUsername == QStringLiteral("voice-first") ? QStringLiteral("voice-second") : QStringLiteral("voice-first"));
		QVERIFY(findMessage(listener, QStringLiteral("VoiceFrame")).isEmpty());

		gtd::CanvasObject fill;
		fill.id = QStringLiteral("shared-fill");
		fill.layerId = QStringLiteral("layer-default");
		fill.type = QStringLiteral("fill");
		fill.color = QStringLiteral("#22aaee");
		fill.fillColor = fill.color;
		fill.imageData = QStringLiteral("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aGZsAAAAASUVORK5CYII=");
		fill.x = 0.25;
		fill.y = 0.25;
		fill.width = 0.5;
		fill.height = 0.5;
		const QJsonObject fillOperation{
			{QStringLiteral("action"), QStringLiteral("object_upsert")},
			{QStringLiteral("object"), fill.toJson()}};
		QVERIFY(gtd::sendMessage(drawer, gtd::makeMessage(QStringLiteral("CanvasOperation"),
			{{QStringLiteral("operation"), fillOperation}})));
		QTRY_VERIFY(!findMessage(listener, QStringLiteral("CanvasOperation")).isEmpty());
		const QJsonObject canvasMessage = findMessage(listener, QStringLiteral("CanvasOperation"));
		QCOMPARE(canvasMessage.value(QStringLiteral("operation")).toObject()
			.value(QStringLiteral("object")).toObject().value(QStringLiteral("type")).toString(),
			QStringLiteral("fill"));

		received[listener].clear();
		QVERIFY(gtd::sendMessage(drawer, gtd::makeMessage(QStringLiteral("VoiceFrame"),
			{{QStringLiteral("sampleRate"), 16000},
			 {QStringLiteral("audio"), QString::fromLatin1(audio.toBase64())}})));
		QVERIFY(gtd::sendMessage(drawer, gtd::makeMessage(QStringLiteral("RoomChat"),
			{{QStringLiteral("requestId"), QStringLiteral("drawer-chat-barrier")},
			 {QStringLiteral("text"), QStringLiteral("still blocked")}})));
		QTRY_VERIFY(!findMessage(drawer, QStringLiteral("Error"), QStringLiteral("drawer-chat-barrier")).isEmpty());
		QVERIFY(findMessage(listener, QStringLiteral("VoiceFrame")).isEmpty());
	}

	void filtersServerMutesAndViewerMutesByChannel()
	{
		QTemporaryDir dictionaryDirectory;
		QVERIFY(dictionaryDirectory.isValid());
		const QString dictionaryPath = dictionaryDirectory.filePath(QStringLiteral("words.txt"));
		QFile dictionary(dictionaryPath);
		QVERIFY(dictionary.open(QIODevice::WriteOnly | QIODevice::Text));
		QCOMPARE(dictionary.write("filterword\n"), qint64(11));
		dictionary.close();

		QTcpServer master;
		QVERIFY(master.listen(QHostAddress::LocalHost, 0));
		gtd::RoomInfo room;
		room.roomId = QStringLiteral("room-mute-filter");
		room.roomName = QStringLiteral("Mute filter room");
		room.maxPlayers = 2;
		gtd::GameServer server(room, QStringLiteral("server-mute-filter"), QStringLiteral("127.0.0.1"),
			QStringLiteral("127.0.0.1"), master.serverPort(), QStringLiteral("server-secret"),
			QStringLiteral("session-secret"), dictionaryPath, 0);
		QString error;
		QVERIFY2(server.start(&error), qPrintable(error));
		QTRY_VERIFY(master.hasPendingConnections());
		QTcpSocket* masterSocket = master.nextPendingConnection();
		QVERIFY(masterSocket);
		QByteArray masterBuffer;
		bool muteSent = false;
		auto processMasterData = [&] {
			for (const QJsonObject& message : gtd::takeMessages(&masterBuffer, masterSocket->readAll())) {
				const QString type = message.value(QStringLiteral("type")).toString();
				if (type == QStringLiteral("ServerRegister")) {
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerRegisterAck"),
						{{QStringLiteral("ok"), true}}));
				} else if (type == QStringLiteral("ServerJoinCheck")) {
					QString username;
					QString displayName;
					gtd::verifySessionToken(message.value(QStringLiteral("authToken")).toString(),
						QStringLiteral("session-secret"), &username, -1, &displayName);
					if (username == QStringLiteral("ban-target")) {
						gtd::ServerSanction ban;
						ban.serverId = room.serverId;
						ban.targetUsername = username;
						ban.kind = QStringLiteral("ban");
						ban.reason = QStringLiteral("testing ban");
						ban.expiresAt = QDateTime::currentSecsSinceEpoch() + 300;
						gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerJoinCheckResult"),
							{{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
							 {QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Banned: testing ban")},
							 {QStringLiteral("ban"), ban.toJson()}}));
						continue;
					}
					gtd::PlayerProfile profile;
					profile.username = username;
					profile.displayName = displayName;
					QJsonArray preferences;
					if (username == QStringLiteral("mute-viewer")) {
						preferences.append(gtd::PlayerMutePreference{room.serverId, username,
							QStringLiteral("mute-other"), true, false}.toJson());
					}
					QJsonArray sanctions;
					if (username == QStringLiteral("mute-target") && !muteSent) {
						gtd::ServerSanction sanction;
						sanction.serverId = room.serverId;
						sanction.targetUsername = username;
						sanction.kind = QStringLiteral("mute");
						sanction.textChat = true;
						sanction.voiceChat = true;
						sanction.reason = QStringLiteral("testing");
						sanction.expiresAt = QDateTime::currentSecsSinceEpoch() + 300;
						sanctions.append(sanction.toJson());
						muteSent = true;
					}
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ServerJoinCheckResult"),
						{{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
						 {QStringLiteral("ok"), true}, {QStringLiteral("username"), username},
						 {QStringLiteral("profile"), profile.toJson()}, {QStringLiteral("sanctions"), sanctions},
						 {QStringLiteral("playerMutes"), preferences}}));
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
				for (const QJsonObject& message : gtd::takeMessages(&buffers[socket], socket->readAll()))
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
		auto join = [&](QTcpSocket* socket, const QString& username) {
			monitor(socket);
			socket->connectToHost(QHostAddress::LocalHost, server.port());
			QTRY_COMPARE(socket->state(), QAbstractSocket::ConnectedState);
			const QString token = gtd::createSessionToken(username, username, QStringLiteral("session-secret"),
				QDateTime::currentSecsSinceEpoch() + 300);
			QVERIFY(gtd::sendMessage(socket, gtd::makeMessage(QStringLiteral("JoinRoom"),
				{{QStringLiteral("requestId"), QStringLiteral("join-%1").arg(username)},
				 {QStringLiteral("roomId"), room.roomId}, {QStringLiteral("authToken"), token}})));
			QTRY_VERIFY(!findMessage(socket, QStringLiteral("JoinResult")).isEmpty());
		};
		QTcpSocket target;
		QTcpSocket viewer;
		QTcpSocket other;
		QTcpSocket banned;
		monitor(&banned);
		banned.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(banned.state(), QAbstractSocket::ConnectedState);
		const QString banToken = gtd::createSessionToken(QStringLiteral("ban-target"), QStringLiteral("ban-target"),
			QStringLiteral("session-secret"), QDateTime::currentSecsSinceEpoch() + 300);
		QVERIFY(gtd::sendMessage(&banned, gtd::makeMessage(QStringLiteral("JoinRoom"),
			{{QStringLiteral("requestId"), QStringLiteral("join-banned")}, {QStringLiteral("roomId"), room.roomId},
			 {QStringLiteral("authToken"), banToken}})));
		QTRY_VERIFY(!findMessage(&banned, QStringLiteral("JoinResult"), QStringLiteral("join-banned")).isEmpty());
		const QJsonObject bannedJoin = findMessage(&banned, QStringLiteral("JoinResult"), QStringLiteral("join-banned"));
		QVERIFY(!bannedJoin.value(QStringLiteral("ok")).toBool());
		QCOMPARE(bannedJoin.value(QStringLiteral("ban")).toObject().value(QStringLiteral("reason")).toString(),
			QStringLiteral("testing ban"));
		QVERIFY(bannedJoin.value(QStringLiteral("ban")).toObject()
			.value(QStringLiteral("expiresAt")).toVariant().toLongLong() > 0);
		banned.disconnectFromHost();

		join(&target, QStringLiteral("mute-target"));
		const QJsonObject activeMuteNotice = findMessage(&target, QStringLiteral("ModerationNotice"));
		QCOMPARE(activeMuteNotice.value(QStringLiteral("reason")).toString(), QStringLiteral("testing"));
		QVERIFY(activeMuteNotice.value(QStringLiteral("expiresAt")).toVariant().toLongLong() > 0);
		join(&viewer, QStringLiteral("mute-viewer"));
		join(&other, QStringLiteral("mute-other"));
		QTRY_VERIFY(!findMessage(&target, QStringLiteral("RoundStarted")).isEmpty());
		QVERIFY(findMessage(&other, QStringLiteral("JoinResult")).value(QStringLiteral("spectator")).toBool());
		const QString drawer = findMessage(&target, QStringLiteral("RoundStarted"))
			.value(QStringLiteral("drawerUsername")).toString();
		QTcpSocket* roundGuesser = drawer == QStringLiteral("mute-target") ? &viewer : &target;
		QVERIFY(gtd::sendMessage(roundGuesser, gtd::makeMessage(QStringLiteral("Guess"),
			{{QStringLiteral("text"), QStringLiteral("filterword")}})));
		QTRY_VERIFY(!findMessage(&target, QStringLiteral("RoundEnded")).isEmpty());
		received[&target].clear();
		received[&viewer].clear();
		received[&other].clear();
		sendRoomChat(&target, QStringLiteral("muted-chat"), QStringLiteral("blocked chat"));
		sendVoiceFrame(&target);
		sendRoomChat(&other, QStringLiteral("spectator-chat"), QStringLiteral("filtered text"));
		sendVoiceFrame(&other);
		QTest::qWait(100);
		QVERIFY(findMessage(&target, QStringLiteral("Error"), QStringLiteral("muted-chat"))
			.value(QStringLiteral("message")).toString().contains(QStringLiteral("muted by server staff")));
		auto receivedText = [&received](QTcpSocket* socket, const QString& text) {
			return std::any_of(received.value(socket).cbegin(), received.value(socket).cend(),
				[&text](const QJsonObject& message) {
					return message.value(QStringLiteral("type")).toString() == QStringLiteral("ChatMessage")
						&& message.value(QStringLiteral("text")).toString() == text;
				});
		};
		QVERIFY(!receivedText(&viewer, QStringLiteral("filtered text")));
		QVERIFY(receivedText(&target, QStringLiteral("filtered text")));
		QVERIFY(!findMessage(&viewer, QStringLiteral("VoiceFrame")).isEmpty());
		QVERIFY(!findMessage(&target, QStringLiteral("VoiceFrame")).isEmpty());

		const auto sendModerationApplied = [&](const QString& action, const QString& targetUsername,
			const QString& reason, bool textChat = false, bool voiceChat = false, qint64 expiresAt = -1) {
			QJsonObject fields{{QStringLiteral("serverId"), room.serverId},
				{QStringLiteral("targetUsername"), targetUsername}, {QStringLiteral("issuedBy"), QStringLiteral("room-admin")},
				{QStringLiteral("action"), action}, {QStringLiteral("reason"), reason}};
			if (action == QStringLiteral("mute")) {
				gtd::ServerSanction sanction;
				sanction.serverId = room.serverId;
				sanction.targetUsername = targetUsername;
				sanction.kind = action;
				sanction.textChat = textChat;
				sanction.voiceChat = voiceChat;
				sanction.reason = reason;
				sanction.expiresAt = expiresAt < 0 ? QDateTime::currentSecsSinceEpoch() + 600 : expiresAt;
				fields.insert(QStringLiteral("sanction"), sanction.toJson());
			}
			return gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ModerationApplied"), fields));
		};
		QVERIFY(sendModerationApplied(QStringLiteral("mute"), QStringLiteral("mute-target"),
			QStringLiteral("Repeated disruption"), true, false));
		QTRY_VERIFY(!findMessage(&target, QStringLiteral("ModerationNotice")).isEmpty());
		QVERIFY(findMessage(&target, QStringLiteral("ModerationNotice"))
			.value(QStringLiteral("textChat")).toBool());
		QVERIFY(!findMessage(&target, QStringLiteral("ModerationNotice"))
			.value(QStringLiteral("voiceChat")).toBool());
		QVERIFY(findMessage(&target, QStringLiteral("ModerationNotice"))
			.value(QStringLiteral("expiresAt")).toVariant().toLongLong() > 0);
		QCOMPARE(findMessage(&target, QStringLiteral("ModerationNotice"))
			.value(QStringLiteral("reason")).toString(), QStringLiteral("Repeated disruption"));
		QVERIFY(findMessage(&target, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QVERIFY(!findMessage(&viewer, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QCOMPARE(target.state(), QAbstractSocket::ConnectedState);
		received[&target].clear();
		QVERIFY(sendModerationApplied(QStringLiteral("mute"), QStringLiteral("mute-target"),
			QStringLiteral("Permanent mute"), true, false, 0));
		QTRY_VERIFY(!findMessage(&target, QStringLiteral("ModerationNotice")).isEmpty());
		QCOMPARE(findMessage(&target, QStringLiteral("ModerationNotice"))
			.value(QStringLiteral("expiresAt")).toVariant().toLongLong(), qint64(0));
		received[&target].clear();
		received[&viewer].clear();
		QVERIFY(sendModerationApplied(QStringLiteral("unmute"), QStringLiteral("mute-target"),
			QStringLiteral("Mute lifted")));
		QVERIFY(findMessage(&target, QStringLiteral("ModerationNotice")).isEmpty());
		QVERIFY(findMessage(&target, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QTRY_VERIFY(!findMessage(&viewer, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QCOMPARE(findMessage(&viewer, QStringLiteral("ModerationAnnouncement"))
			.value(QStringLiteral("action")).toString(), QStringLiteral("unmute"));

		received[&target].clear();
		received[&viewer].clear();
		QVERIFY(sendModerationApplied(QStringLiteral("kick"), QStringLiteral("mute-target"),
			QStringLiteral("Repeated disruption")));
		QTRY_COMPARE(target.state(), QAbstractSocket::UnconnectedState);
		const QJsonObject kickNotice = findMessage(&target, QStringLiteral("ModerationNotice"));
		QCOMPARE(kickNotice.value(QStringLiteral("issuedBy")).toString(), QStringLiteral("room-admin"));
		QCOMPARE(kickNotice.value(QStringLiteral("reason")).toString(), QStringLiteral("Repeated disruption"));
		QVERIFY(findMessage(&target, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QVERIFY(!findMessage(&viewer, QStringLiteral("ModerationAnnouncement")).isEmpty());

		received[&other].clear();
		received[&viewer].clear();
		QVERIFY(sendModerationApplied(QStringLiteral("ban"), QStringLiteral("mute-other"),
			QStringLiteral("Repeated disruption")));
		QTRY_COMPARE(other.state(), QAbstractSocket::UnconnectedState);
		QVERIFY(!findMessage(&other, QStringLiteral("ModerationNotice")).isEmpty());
		QVERIFY(findMessage(&other, QStringLiteral("ModerationAnnouncement")).isEmpty());
		QVERIFY(!findMessage(&viewer, QStringLiteral("ModerationAnnouncement")).isEmpty());
		received[&viewer].clear();
		QVERIFY(sendModerationApplied(QStringLiteral("unban"), QStringLiteral("mute-other"),
			QStringLiteral("Ban lifted")));
		QTRY_VERIFY(!findMessage(&viewer, QStringLiteral("ModerationAnnouncement")).isEmpty());
		const QJsonObject unbanAnnouncement = findMessage(&viewer, QStringLiteral("ModerationAnnouncement"));
		QCOMPARE(unbanAnnouncement.value(QStringLiteral("action")).toString(), QStringLiteral("unban"));
		QCOMPARE(unbanAnnouncement.value(QStringLiteral("targetUsername")).toString(), QStringLiteral("mute-other"));
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
				} else if (type == QStringLiteral("ServerJoinCheck")) {
					respondToJoinCheck(masterSocket, message);
				} else if (type == QStringLiteral("ProfileRequest")) {
					profileRequestReceived = true;
					gtd::sendMessage(masterSocket, gtd::makeMessage(QStringLiteral("ProfileResult"),
						{{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
						 {QStringLiteral("profile"), QJsonObject{
							 {QStringLiteral("username"), QStringLiteral("ranked-player")},
							 {QStringLiteral("displayName"), QStringLiteral("Ranked Player")},
							 {QStringLiteral("countryCode"), QStringLiteral("XK")},
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
		QCOMPARE(latestRosterProfile.value(QStringLiteral("countryCode")).toString(), QStringLiteral("XK"));
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
				} else if (type == QStringLiteral("ServerJoinCheck")) {
					respondToJoinCheck(masterSocket, message);
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
				} else if (type == QStringLiteral("ServerJoinCheck")) {
					respondToJoinCheck(masterSocket, message);
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
