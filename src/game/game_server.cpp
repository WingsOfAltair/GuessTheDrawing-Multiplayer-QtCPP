#include "game/game_server.h"

#include "shared/auth_token.h"
#include "shared/game_rules.h"
#include "shared/protocol.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QHostAddress>
#include <QDebug>
#include <QTextStream>

#include <cstdio>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gtd {
namespace {

QJsonObject reply(const QString& type, const QJsonObject& request, const QJsonObject& fields = {})
{
	QJsonObject response = makeMessage(type, fields);
	const QJsonValue requestId = request.value(QStringLiteral("requestId"));
	if (!requestId.isUndefined())
		response.insert(QStringLiteral("requestId"), requestId);
	return response;
}

void writeConsoleStatus(const QString& status)
{
#if defined(Q_OS_WIN)
	const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO screenInfo{};
	if (console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(console, &screenInfo)) {
		const COORD cursorPosition = screenInfo.dwCursorPosition;
		if (SetConsoleCursorPosition(console, COORD{0, screenInfo.srWindow.Top})) {
			const int lineWidth = qMax(0, static_cast<int>(screenInfo.dwSize.X) - 1);
			QTextStream output(stdout);
			output << status.left(lineWidth).leftJustified(lineWidth) << Qt::flush;
			SetConsoleCursorPosition(console, cursorPosition);
			return;
		}
	}
#else
	QTextStream output(stdout);
	output << "\x1b[s\x1b[1;1H\x1b[2K" << status << "\x1b[u" << Qt::flush;
	return;
#endif
	static qsizetype previousStatusWidth = 0;
	const QString paddedStatus = status.leftJustified(qMax(status.size(), previousStatusWidth));
	previousStatusWidth = paddedStatus.size();
	QTextStream output(stdout);
	output << QLatin1Char('\r') << paddedStatus << Qt::flush;
}

}

GameServer::GameServer(RoomInfo room, QString serverId, QString publicHost, QString masterHost,
					   quint16 masterPort, QString serverSecret, QString sessionSecret,
					   QString dictionaryPath, int intermissionSeconds, QObject* parent)
	: QObject(parent),
	  room_(std::move(room)),
	  serverId_(std::move(serverId)),
	  publicHost_(std::move(publicHost)),
	  masterHost_(std::move(masterHost)),
	  masterPort_(masterPort),
	  serverSecret_(std::move(serverSecret)),
	  sessionSecret_(std::move(sessionSecret)),
	  dictionaryPath_(dictionaryPath.trimmed().isEmpty()
		? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dictionary"))
		: std::move(dictionaryPath)),
	  intermissionSeconds_(qBound(0, intermissionSeconds, 120))
{
	room_.serverId = serverId_;
	room_.roomId = room_.roomId.trimmed();
	room_.roomName = room_.roomName.trimmed();
	room_.roundSeconds = clampRoundSeconds(room_.roundSeconds);
	room_.maxPlayers = qBound(2, room_.maxPlayers, 32);
	resetCanvas();
	actionRateLimitClock_.start();

	connect(&listener_, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
	masterSocket_.setReadBufferSize(MaximumFrameBytes);
	connect(&masterSocket_, &QTcpSocket::connected, this, [this] {
		reconnectTimer_.stop();
		masterRegistered_ = false;
		qInfo().noquote() << QStringLiteral("Connected to master at %1:%2").arg(masterHost_).arg(masterPort_);
		sendMasterRegistration(true);
	});
	connect(&masterSocket_, &QTcpSocket::readyRead, this, [this] { readMaster(); });
	connect(&masterSocket_, &QTcpSocket::disconnected, this, [this] {
		if (masterRegistered_)
			qWarning().noquote() << QStringLiteral("Connection to master lost; retrying in 3 seconds");
		masterRegistered_ = false;
		for (const PendingJoinRequest& pending : std::as_const(pendingJoinRequests_)) {
			if (pending.socket && pending.socket->state() == QAbstractSocket::ConnectedState)
				sendMessage(pending.socket, reply(QStringLiteral("JoinResult"), pending.request,
					{{QStringLiteral("ok"), false},
					 {QStringLiteral("error"), QStringLiteral("Unable to verify access with the master server")}}));
		}
		pendingJoinRequests_.clear();
		pendingJoinBySocket_.clear();
		reconnectTimer_.start();
	});
	connect(&masterSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		qWarning().noquote() << QStringLiteral("Master connection error: %1; retrying in 3 seconds")
			.arg(masterSocket_.errorString());
		reconnectTimer_.start();
	});

	heartbeatTimer_.setInterval(5000);
	connect(&heartbeatTimer_, &QTimer::timeout, this, [this] {
		if (masterRegistered_)
			sendMasterRegistration(false);
	});
	heartbeatTimer_.start();

	reconnectTimer_.setInterval(3000);
	connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
		if (masterSocket_.state() == QAbstractSocket::UnconnectedState) {
			qInfo().noquote() << QStringLiteral("Retrying connection to master at %1:%2")
				.arg(masterHost_).arg(masterPort_);
			connectToMaster();
		}
	});

	roundTimer_.setSingleShot(true);
	connect(&roundTimer_, &QTimer::timeout, this, [this] { endRound(QStringLiteral("time")); });
	countdownTimer_.setInterval(1000);
	connect(&countdownTimer_, &QTimer::timeout, this, [this] {
		if (roundStarting_) {
			const int remaining = qMax(0, static_cast<int>(QDateTime::currentSecsSinceEpoch()
				- intermissionDeadline_.toSecsSinceEpoch()) * -1);
			broadcast(makeMessage(QStringLiteral("IntermissionCountdown"),
				{{QStringLiteral("secondsRemaining"), remaining}}));
			return;
		}
		if (!roundActive_)
			return;
		const int remaining = qMax(0, static_cast<int>(QDateTime::currentSecsSinceEpoch()
			- roundDeadline_.toSecsSinceEpoch()) * -1);
		broadcast(makeMessage(QStringLiteral("Countdown"),
							  {{QStringLiteral("secondsRemaining"), remaining}}));
		if (remaining == 0)
			endRound(QStringLiteral("time"));
	});
	intermissionTimer_.setSingleShot(true);
	connect(&intermissionTimer_, &QTimer::timeout, this, [this] {
		if (!roundStarting_)
			return;
		roundStarting_ = false;
		countdownTimer_.stop();
		startRoundNow();
	});
}

GameServer::~GameServer()
{
	listener_.close();
	for (QTcpSocket* socket : playerBuffers_.keys())
		disconnect(socket, nullptr, this, nullptr);
	heartbeatTimer_.stop();
	reconnectTimer_.stop();
	roundTimer_.stop();
	intermissionTimer_.stop();
	countdownTimer_.stop();
	disconnect(&masterSocket_, nullptr, this, nullptr);
	masterSocket_.abort();
}

bool GameServer::start(QString* error)
{
	if (room_.roomId.isEmpty() || room_.roomName.isEmpty() || serverId_.isEmpty()
		|| serverSecret_.isEmpty() || sessionSecret_.isEmpty()) {
		if (error)
			*error = QStringLiteral("Server ID, room ID/name, and secrets are required");
		return false;
	}
	if (!loadDictionary(error))
		return false;
	if (!listener_.listen(QHostAddress::AnyIPv4, room_.port)) {
		if (error)
			*error = listener_.errorString();
		return false;
	}
	room_.port = listener_.serverPort();
	QTimer::singleShot(0, this, [this] { updateConsoleStatus(); });
	connectToMaster();
	return true;
}

quint16 GameServer::port() const
{
	return listener_.serverPort();
}

void GameServer::acceptConnections()
{
	while (listener_.hasPendingConnections()) {
		QTcpSocket* socket = listener_.nextPendingConnection();
		socket->setReadBufferSize(MaximumFrameBytes);
		playerBuffers_.insert(socket, {});
		socketRateLimits_.insert(socket, {});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readPlayer(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			removePlayer(socket);
			socket->deleteLater();
		});
	}
}

bool GameServer::allowAction(QTcpSocket* socket, const QString& type)
{
	int limit = 30;
	QString bucket = QStringLiteral("other");
	if (type == QStringLiteral("Ping")) {
		bucket = type;
		limit = 20;
	} else if (type == QStringLiteral("JoinRoom")) {
		bucket = type;
		limit = 3;
	} else if (type == QStringLiteral("Guess")) {
		bucket = type;
		limit = 8;
	} else if (type == QStringLiteral("RoomChat")) {
		bucket = type;
		limit = 8;
	} else if (type == QStringLiteral("VoiceFrame")) {
		bucket = type;
		limit = 60;
	} else if (type == QStringLiteral("DrawStroke")) {
		bucket = type;
		limit = 100;
	} else if (type == QStringLiteral("CanvasOperation")) {
		bucket = type;
		limit = 60;
	} else if (type == QStringLiteral("ClearCanvas")) {
		bucket = type;
		limit = 10;
	} else if (type == QStringLiteral("ChangeRole") || type == QStringLiteral("PromoteToPlayer")) {
		bucket = QStringLiteral("role");
		limit = 5;
	} else if (type == QStringLiteral("LeaveRoom")) {
		bucket = type;
		limit = 2;
	}

	const qint64 now = actionRateLimitClock_.elapsed();
	SocketRateLimit& socketLimit = socketRateLimits_[socket];
	ActionWindow& actionWindow = socketLimit.actions[bucket];
	if (actionWindow.startedAtMs < 0 || now - actionWindow.startedAtMs >= 1000) {
		actionWindow.startedAtMs = now;
		actionWindow.count = 0;
	}
	if (actionWindow.count < limit) {
		++actionWindow.count;
		return true;
	}

	if (socketLimit.strikeWindowStartedAtMs < 0 || now - socketLimit.strikeWindowStartedAtMs >= 60000) {
		socketLimit.strikeWindowStartedAtMs = now;
		socketLimit.strikes = 0;
	}
	if (++socketLimit.strikes >= 10) {
		qWarning().noquote() << QStringLiteral("Disconnecting %1 after repeated rate-limit violations")
			.arg(playersBySocket_.value(socket, socket->peerAddress().toString()));
		socket->disconnectFromHost();
	}
	return false;
}

void GameServer::readPlayer(QTcpSocket* socket)
{
	QString error;
	const QList<QJsonObject> messages = takeMessages(&playerBuffers_[socket], socket->readAll(), &error);
	if (!error.isEmpty()) {
		sendMessage(socket, makeMessage(QStringLiteral("Error"),
										{{QStringLiteral("message"), error}}));
		socket->disconnectFromHost();
		return;
	}
	for (const QJsonObject& message : messages)
		handlePlayerMessage(socket, message);
}

void GameServer::handlePlayerMessage(QTcpSocket* socket, const QJsonObject& message)
{
	const QString type = message.value(QStringLiteral("type")).toString();
	if (!allowAction(socket, type))
		return;
	if (type == QStringLiteral("Ping")) {
		sendMessage(socket, reply(QStringLiteral("Pong"), message));
		return;
	}
	if (!playersBySocket_.contains(socket)) {
		if (type == QStringLiteral("JoinRoom"))
			joinRoom(socket, message);
		else
			sendMessage(socket, reply(QStringLiteral("Error"), message,
									  {{QStringLiteral("message"), QStringLiteral("Join a room first")}}));
		return;
	}

	const QString username = playersBySocket_.value(socket);
	if (type == QStringLiteral("LeaveRoom")) {
		sendMessage(socket, reply(QStringLiteral("LeaveAck"), message));
		socket->disconnectFromHost();
	} else if (type == QStringLiteral("PromoteToPlayer") || type == QStringLiteral("ChangeRole")) {
		changePlayerRole(socket, message);
	} else if (type == QStringLiteral("Guess")) {
		const auto player = players_.constFind(username);
		if (player == players_.cend() || player->spectator) {
			sendMessage(socket, reply(QStringLiteral("Error"), message,
								  {{QStringLiteral("message"), QStringLiteral("Spectators cannot guess")}}));
			return;
		}
		sendGuess(username, message.value(QStringLiteral("text")).toString());
	} else if (type == QStringLiteral("RoomChat")) {
		const auto player = players_.constFind(username);
		if (player != players_.cend() && player->serverTextMuted
			&& (player->serverMuteExpiresAt == 0 || player->serverMuteExpiresAt > QDateTime::currentSecsSinceEpoch())) {
			sendMessage(socket, reply(QStringLiteral("Error"), message,
				{{QStringLiteral("message"), QStringLiteral("Your text chat is muted by server staff")}}));
			return;
		}
		if (roundActive_ && username == currentDrawer_) {
			sendMessage(socket, reply(QStringLiteral("Error"), message,
				{{QStringLiteral("message"), QStringLiteral("The drawer cannot send chat during the round")}}));
			return;
		}
		const QString text = message.value(QStringLiteral("text")).toString().trimmed();
		if (player == players_.cend() || !isValidSingleLineText(text, 300))
			return;
		const QJsonObject chatMessage = makeMessage(QStringLiteral("ChatMessage"),
			{{QStringLiteral("username"), username}, {QStringLiteral("displayName"), player->displayName},
			 {QStringLiteral("text"), text}, {QStringLiteral("roomChat"), true}});
		for (const Player& recipient : players_) {
			if (!recipient.mutedTextUsers.contains(username))
				sendMessage(recipient.socket, chatMessage);
		}
	} else if (type == QStringLiteral("VoiceFrame")) {
		const auto player = players_.constFind(username);
		if (player == players_.cend() || (player->serverVoiceMuted
			&& (player->serverMuteExpiresAt == 0
				|| player->serverMuteExpiresAt > QDateTime::currentSecsSinceEpoch()))
			|| (roundActive_ && username == currentDrawer_))
			return;
		const int sampleRate = message.value(QStringLiteral("sampleRate")).toInt();
		const QByteArray encodedAudio = message.value(QStringLiteral("audio")).toString().toLatin1();
		if (sampleRate != 16000 || encodedAudio.size() != 856)
			return;
		const QByteArray audio = QByteArray::fromBase64(encodedAudio);
		if (audio.size() != 640 || audio.toBase64() != encodedAudio)
			return;
		const QJsonObject frame = makeMessage(QStringLiteral("VoiceFrame"),
			{{QStringLiteral("username"), username}, {QStringLiteral("displayName"), player->displayName},
			 {QStringLiteral("sampleRate"), sampleRate},
			 {QStringLiteral("audio"), QString::fromLatin1(encodedAudio)}});
		for (const Player& recipient : players_) {
			if (recipient.username != username && !recipient.mutedVoiceUsers.contains(username))
				sendMessage(recipient.socket, frame);
		}
	} else if (type == QStringLiteral("DrawStroke")) {
		if (!roundActive_ || username != currentDrawer_) {
			sendMessage(socket, reply(QStringLiteral("Error"), message,
									  {{QStringLiteral("message"), QStringLiteral("Only the current drawer can draw")}}));
			return;
		}
		DrawingStroke stroke;
		if (!validStroke(message.value(QStringLiteral("stroke")).toObject(), &stroke)) {
			sendMessage(socket, reply(QStringLiteral("Error"), message,
									  {{QStringLiteral("message"), QStringLiteral("Invalid drawing stroke")}}));
			return;
		}
		QJsonObject event = makeMessage(QStringLiteral("DrawStroke"),
			{{QStringLiteral("username"), username}, {QStringLiteral("stroke"), stroke.toJson()}});
		for (const Player& player : players_) {
			if (player.username != username)
				sendMessage(player.socket, event);
		}
	} else if (type == QStringLiteral("ClearCanvas")) {
		if (roundActive_ && username == currentDrawer_) {
			canvasObjects_.clear();
			broadcast(makeMessage(QStringLiteral("CanvasState"),
				{{QStringLiteral("layers"), canvasLayersJson()},
				 {QStringLiteral("objects"), canvasObjectsJson()}}));
		}
	} else if (type == QStringLiteral("CanvasOperation")) {
		handleCanvasOperation(socket, message);
	} else {
		sendMessage(socket, reply(QStringLiteral("Error"), message,
								  {{QStringLiteral("message"), QStringLiteral("Unknown room request")}}));
	}
}

void GameServer::joinRoom(QTcpSocket* socket, const QJsonObject& message)
{
	if (message.value(QStringLiteral("roomId")).toString() != room_.roomId) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
								  {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Room not found")}}));
		return;
	}
	QString username;
	QString displayName;
	if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
							&username, -1, &displayName)) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
								  {{QStringLiteral("ok"), false},
								   {QStringLiteral("error"), QStringLiteral("Session expired or invalid")}}));
		return;
	}
	username = username.toLower();
	if (players_.contains(username)) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
								  {{QStringLiteral("ok"), false},
								   {QStringLiteral("error"), QStringLiteral("That account is already in this room")}}));
		return;
	}
	if (pendingJoinBySocket_.contains(socket)) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Join request is already pending")}}));
		return;
	}
	if (!masterRegistered_ || masterSocket_.state() != QAbstractSocket::ConnectedState) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Master server is unavailable")}}));
		return;
	}
	const QString requestId = QString::number(nextMasterRequestId_++);
	PendingJoinRequest pending;
	pending.socket = socket;
	pending.request = message;
	pending.username = username;
	pending.displayName = displayName;
	pendingJoinRequests_.insert(requestId, pending);
	pendingJoinBySocket_.insert(socket, requestId);
	if (!sendMessage(&masterSocket_, makeMessage(QStringLiteral("ServerJoinCheck"),
		{{QStringLiteral("requestId"), requestId},
		 {QStringLiteral("authToken"), message.value(QStringLiteral("authToken")).toString()}}))) {
		pendingJoinRequests_.remove(requestId);
		pendingJoinBySocket_.remove(socket);
		sendMessage(socket, reply(QStringLiteral("JoinResult"), message,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unable to verify access with the master server")}}));
	}
}

void GameServer::completeJoinRoom(const PendingJoinRequest& pending, const QJsonObject& authorization)
{
	QTcpSocket* socket = pending.socket;
	const QJsonObject& request = pending.request;
	if (!socket || socket->state() != QAbstractSocket::ConnectedState || !playerBuffers_.contains(socket))
		return;
	if (!authorization.value(QStringLiteral("ok")).toBool()) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), pending.request,
			{{QStringLiteral("ok"), false},
			 {QStringLiteral("error"), authorization.value(QStringLiteral("error")).toString(
				QStringLiteral("Join rejected by the master server"))}}));
		return;
	}
	const PlayerProfile profile = PlayerProfile::fromJson(authorization.value(QStringLiteral("profile")).toObject());
	if (profile.username != pending.username || players_.contains(pending.username)) {
		sendMessage(socket, reply(QStringLiteral("JoinResult"), pending.request,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Master authorization did not match this account")}}));
		return;
	}
	const QString username = pending.username;
	const QString displayName = pending.displayName;
	const bool requestedSpectator = request.value(QStringLiteral("spectator")).toBool(false);
	const bool queuedForPlayer = !requestedSpectator
		&& (activePlayerCount() >= room_.maxPlayers || !playerQueue_.isEmpty());
	const bool spectator = requestedSpectator || queuedForPlayer;

	Player player;
	player.socket = socket;
	player.username = username;
	player.displayName = profile.displayName.isEmpty()
		? (displayName.isEmpty() ? username : displayName) : profile.displayName;
	player.avatarUrl = profile.avatarUrl;
	player.countryCode = profile.countryCode;
	player.guessingScore = profile.guessingScore;
	player.drawingScore = profile.drawingScore;
	player.roundsWon = profile.roundsWon;
	player.roundsLost = profile.roundsLost;
	player.roundsAbandoned = profile.roundsAbandoned;
	player.leaderboardRank = profile.leaderboardRank;
	player.guessingLeaderboardRank = profile.guessingLeaderboardRank;
	player.drawingLeaderboardRank = profile.drawingLeaderboardRank;
	player.guessingRank = profile.guessingRank;
	player.drawingRank = profile.drawingRank;
	player.serverRole = profile.serverRole;
	player.masterSuperAdmin = profile.masterSuperAdmin;
	player.authToken = request.value(QStringLiteral("authToken")).toString();
	player.spectator = spectator;
	player.queuedForPlayer = queuedForPlayer;
	for (const QJsonValue& value : authorization.value(QStringLiteral("sanctions")).toArray()) {
		const ServerSanction sanction = ServerSanction::fromJson(value.toObject());
		if (sanction.kind == QStringLiteral("mute")) {
			player.serverTextMuted = player.serverTextMuted || sanction.textChat;
			player.serverVoiceMuted = player.serverVoiceMuted || sanction.voiceChat;
			if (sanction.textChat || sanction.voiceChat)
				player.serverMuteExpiresAt = player.serverMuteExpiresAt == 0 || sanction.expiresAt == 0
					? 0 : qMax(player.serverMuteExpiresAt, sanction.expiresAt);
		}
	}
	for (const QJsonValue& value : authorization.value(QStringLiteral("playerMutes")).toArray()) {
		const PlayerMutePreference preference = PlayerMutePreference::fromJson(value.toObject());
		if (preference.textChat)
			player.mutedTextUsers.insert(preference.targetUsername);
		if (preference.voiceChat)
			player.mutedVoiceUsers.insert(preference.targetUsername);
	}
	players_.insert(username, player);
	playerOrder_.append(username);
	playersBySocket_.insert(socket, username);
	if (queuedForPlayer)
		playerQueue_.append(username);
	updateConsoleStatus();
	logEvent(QStringLiteral("Player joined: %1 (@%2) as %3%4")
		.arg(player.displayName, username,
			spectator ? QStringLiteral("spectator") : QStringLiteral("player"),
			queuedForPlayer ? QStringLiteral(" (queued for a player slot)") : QString()));

	sendMessage(socket, reply(QStringLiteral("JoinResult"), request,
		{{QStringLiteral("ok"), true}, {QStringLiteral("room"), currentRoomInfo().toJson()},
		 {QStringLiteral("players"), roster()}, {QStringLiteral("roundActive"), roundActive_},
			 {QStringLiteral("spectator"), spectator},
			 {QStringLiteral("queuedForPlayer"), queuedForPlayer},
		 {QStringLiteral("roundStarting"), roundStarting_},
			 {QStringLiteral("queue"), queueStatus(username)},
		 {QStringLiteral("drawerUsername"), currentDrawer_},
		 {QStringLiteral("layers"), canvasLayersJson()},
		 {QStringLiteral("objects"), canvasObjectsJson()},
		 {QStringLiteral("secondsRemaining"), roundActive_
			? qMax(0, static_cast<int>(QDateTime::currentSecsSinceEpoch()
				- roundDeadline_.toSecsSinceEpoch()) * -1)
			: roundStarting_ ? qMax(0, static_cast<int>(intermissionDeadline_.toSecsSinceEpoch()
				- QDateTime::currentSecsSinceEpoch())) : 0}}));
	broadcastRoster();
	broadcastQueueStatus();
	updateMasterRoom();
	requestProfile(players_.value(username));
	processPlayerQueue();
	if (!roundActive_ && activePlayerCount() >= 2)
		startRound();
}

void GameServer::promoteToPlayer(QTcpSocket* socket, const QJsonObject& request)
{
	changePlayerRole(socket, request);
}

void GameServer::changePlayerRole(QTcpSocket* socket, const QJsonObject& request)
{
	const QString username = playersBySocket_.value(socket);
	auto player = players_.find(username);
	if (player == players_.end()) {
		sendMessage(socket, reply(QStringLiteral("RoleChangeResult"), request,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("You are not in this room")}}));
		return;
	}
	const bool spectator = request.value(QStringLiteral("spectator")).toBool(false);
	if (spectator && player->queuedForPlayer) {
		removeFromPlayerQueue(username);
		sendMessage(socket, reply(QStringLiteral("RoleChangeResult"), request,
			{{QStringLiteral("ok"), true}, {QStringLiteral("spectator"), true},
			 {QStringLiteral("queue"), queueStatus(username)}}));
		broadcastQueueStatus();
		updateConsoleStatus();
		return;
	}
	if (player->spectator == spectator) {
		sendMessage(socket, reply(QStringLiteral("RoleChangeResult"), request,
			{{QStringLiteral("ok"), true}, {QStringLiteral("spectator"), spectator}}));
		return;
	}
	if (!spectator && (activePlayerCount() >= room_.maxPlayers || !playerQueue_.isEmpty())) {
		if (!player->queuedForPlayer)
			enqueueForPlayer(username);
		sendMessage(socket, reply(QStringLiteral("RoleChangeResult"), request,
			{{QStringLiteral("ok"), true}, {QStringLiteral("spectator"), true},
			 {QStringLiteral("queued"), true}, {QStringLiteral("queue"), queueStatus(username)}}));
		broadcastQueueStatus();
		updateConsoleStatus();
		return;
	}
	const bool wasDrawer = username == currentDrawer_;
	if (spectator)
		markRoundAbandoned(username);
	if (spectator && player->guessed)
		--correctGuesses_;
	if (spectator)
		removeFromPlayerQueue(username);
	player->spectator = spectator;
	player->queuedForPlayer = false;
	if (!spectator)
		player->guessed = false;
	updateConsoleStatus();
	sendMessage(socket, reply(QStringLiteral("RoleChangeResult"), request,
		{{QStringLiteral("ok"), true}, {QStringLiteral("spectator"), spectator}}));
	broadcastRoster();
	broadcastQueueStatus();
	updateMasterRoom();
	if (wasDrawer && roundActive_)
		endRound(QStringLiteral("drawer_left"));
	else if (roundActive_ && activePlayerCount() <= correctGuesses_ + 1)
		endRound(QStringLiteral("all_guessed"));
	cancelIntermissionIfInsufficientPlayers();
	if (!roundActive_ && activePlayerCount() >= 2)
		startRound();
	processPlayerQueue();
}

void GameServer::removePlayer(QTcpSocket* socket)
{
	playerBuffers_.remove(socket);
	socketRateLimits_.remove(socket);
	const QString pendingRequestId = pendingJoinBySocket_.take(socket);
	if (!pendingRequestId.isEmpty())
		pendingJoinRequests_.remove(pendingRequestId);
	const QString username = playersBySocket_.take(socket);
	if (username.isEmpty())
		return;
	const auto departing = players_.constFind(username);
	const bool wasSpectator = departing != players_.cend() && departing->spectator;
	if (departing != players_.cend()) {
		logEvent(QStringLiteral("Player left: %1 (@%2) as %3")
			.arg(departing->displayName, username,
				wasSpectator ? QStringLiteral("spectator") : QStringLiteral("player")));
	}
	removeFromPlayerQueue(username);
	const bool wasDrawer = username == currentDrawer_;
	markRoundAbandoned(username);
	players_.remove(username);
	playerOrder_.removeAll(username);
	updateConsoleStatus();
	broadcast(makeMessage(QStringLiteral("PlayerLeft"), {{QStringLiteral("username"), username}}));
	broadcastRoster();
	broadcastQueueStatus();
	updateMasterRoom();

	if (wasSpectator)
		return;
	if (wasDrawer && roundActive_)
		endRound(QStringLiteral("drawer_left"));
	else if (roundActive_ && activePlayerCount() <= correctGuesses_ + 1)
		endRound(QStringLiteral("all_guessed"));
	else if (activePlayerCount() < 2 && roundActive_) {
		roundActive_ = false;
		roundTimer_.stop();
		countdownTimer_.stop();
		broadcast(makeMessage(QStringLiteral("RoundPaused"),
							  {{QStringLiteral("message"), QStringLiteral("Waiting for another player")}}));
	}
	cancelIntermissionIfInsufficientPlayers();
	processPlayerQueue();
}

RoomInfo GameServer::currentRoomInfo() const
{
	RoomInfo room = room_;
	room.players = activePlayerCount();
	room.spectators = spectatorCount();
	return room;
}

int GameServer::activePlayerCount() const
{
	int count = 0;
	for (const Player& player : players_) {
		if (!player.spectator)
			++count;
	}
	return count;
}

void GameServer::enqueueForPlayer(const QString& username)
{
	auto player = players_.find(username);
	if (player == players_.end())
		return;
	player->queuedForPlayer = true;
	player->spectator = true;
	if (!playerQueue_.contains(username))
		playerQueue_.append(username);
}

void GameServer::removeFromPlayerQueue(const QString& username)
{
	playerQueue_.removeAll(username);
	if (auto player = players_.find(username); player != players_.end())
		player->queuedForPlayer = false;
}

QJsonObject GameServer::queueStatus(const QString& username) const
{
	const int index = playerQueue_.indexOf(username);
	return {{QStringLiteral("queued"), index >= 0},
		{QStringLiteral("position"), index >= 0 ? index + 1 : 0},
		{QStringLiteral("ahead"), index >= 0 ? index : 0},
		{QStringLiteral("total"), playerQueue_.size()}};
}

void GameServer::broadcastQueueStatus()
{
	updateConsoleStatus();
	for (const QString& username : playerOrder_) {
		const auto player = players_.constFind(username);
		if (player != players_.cend())
			sendMessage(player->socket, makeMessage(QStringLiteral("QueueStatus"), queueStatus(username)));
	}
}

void GameServer::logEvent(const QString& message)
{
	QString event = message.left(512);
	event.replace(QLatin1Char('\n'), QLatin1Char(' '));
	event.replace(QLatin1Char('\r'), QLatin1Char(' '));
	event.replace(QLatin1Char('\t'), QLatin1Char(' '));
	qInfo().noquote() << event;
	if (masterRegistered_ && !event.isEmpty())
		sendMessage(&masterSocket_, makeMessage(QStringLiteral("ServerEvent"),
			{{QStringLiteral("message"), event}}));
}

void GameServer::updateConsoleStatus()
{
	const int players = activePlayerCount();
	const int queue = playerQueue_.size();
	const int spectators = qMax(0, spectatorCount() - queue);
	if (players == lastStatusPlayerCount_ && spectators == lastStatusSpectatorCount_
		&& queue == lastStatusQueueCount_)
		return;
	lastStatusPlayerCount_ = players;
	lastStatusSpectatorCount_ = spectators;
	lastStatusQueueCount_ = queue;
	writeConsoleStatus(QStringLiteral("Players: %1 | Spectators: %2 | In queue: %3")
		.arg(players).arg(spectators).arg(queue));
}

void GameServer::processPlayerQueue()
{
	bool changed = false;
	while (activePlayerCount() < room_.maxPlayers && !playerQueue_.isEmpty()) {
		const QString username = playerQueue_.takeFirst();
		auto player = players_.find(username);
		if (player == players_.end() || !player->queuedForPlayer)
			continue;
		player->queuedForPlayer = false;
		player->spectator = false;
		player->guessed = false;
		sendMessage(player->socket, makeMessage(QStringLiteral("RoleChanged"),
			{{QStringLiteral("spectator"), false}}));
		changed = true;
	}
	if (changed) {
		updateConsoleStatus();
		broadcastRoster();
		updateMasterRoom();
		if (!roundActive_ && activePlayerCount() >= 2)
			startRound();
	}
	broadcastQueueStatus();
}

int GameServer::spectatorCount() const
{
	int count = 0;
	for (const Player& player : players_) {
		if (player.spectator)
			++count;
	}
	return count;
}

QJsonArray GameServer::roster() const
{
	QJsonArray result;
	for (const QString& username : playerOrder_) {
		const auto iterator = players_.constFind(username);
		if (iterator == players_.cend())
			continue;
		const Player& player = iterator.value();
		PlayerProfile profile;
		profile.username = player.username;
		profile.displayName = player.displayName;
		profile.avatarUrl = player.avatarUrl;
		profile.countryCode = player.countryCode;
		profile.guessingScore = player.guessingScore;
		profile.drawingScore = player.drawingScore;
		profile.totalScore = player.guessingScore + player.drawingScore;
		profile.roundsWon = player.roundsWon;
		profile.roundsLost = player.roundsLost;
		profile.roundsAbandoned = player.roundsAbandoned;
		profile.serverRole = player.serverRole;
		profile.masterSuperAdmin = player.masterSuperAdmin;
		profile.leaderboardRank = player.leaderboardRank;
		profile.guessingLeaderboardRank = player.guessingLeaderboardRank;
		profile.drawingLeaderboardRank = player.drawingLeaderboardRank;
		profile.guessingRank = player.guessingRank;
		profile.drawingRank = player.drawingRank;
		result.append(QJsonObject{{QStringLiteral("profile"), profile.toJson()},
								 {QStringLiteral("roomScore"), player.roomScore},
								 {QStringLiteral("guessed"), player.guessed},
								 {QStringLiteral("spectator"), player.spectator}});
	}
	return result;
}

void GameServer::sendRoomSnapshot(QTcpSocket* socket)
{
	QJsonObject snapshot = makeMessage(QStringLiteral("RoomSnapshot"),
		{{QStringLiteral("room"), currentRoomInfo().toJson()}, {QStringLiteral("players"), roster()},
		 {QStringLiteral("drawerUsername"), currentDrawer_}, {QStringLiteral("roundActive"), roundActive_}});
	sendMessage(socket, snapshot);
}

void GameServer::broadcast(const QJsonObject& message)
{
	for (const Player& player : players_)
		sendMessage(player.socket, message);
}

void GameServer::broadcastRoster()
{
	broadcast(makeMessage(QStringLiteral("RoomRoster"),
		{{QStringLiteral("players"), roster()}, {QStringLiteral("room"), currentRoomInfo().toJson()}}));
}

void GameServer::updateMasterRoom()
{
	if (!masterRegistered_)
		return;
	sendMasterRegistration(false);
}

void GameServer::connectToMaster()
{
	if (masterSocket_.state() == QAbstractSocket::UnconnectedState) {
		reconnectTimer_.stop();
		masterSocket_.connectToHost(masterHost_, masterPort_);
	}
}

void GameServer::readMaster()
{
	QString error;
	const QList<QJsonObject> messages = takeMessages(&masterBuffer_, masterSocket_.readAll(), &error);
	if (!error.isEmpty()) {
		qWarning() << "Invalid master response:" << error;
		masterSocket_.disconnectFromHost();
		return;
	}
	for (const QJsonObject& message : messages) {
		const QString type = message.value(QStringLiteral("type")).toString();
		if (type == QStringLiteral("ServerRegisterAck")) {
			masterRegistered_ = message.value(QStringLiteral("ok")).toBool();
			if (masterRegistered_) {
				sendMasterRegistration(false);
				for (const Player& player : players_)
					requestProfile(player);
			}
		} else if (type == QStringLiteral("ProfileResult")) {
			const QString requestId = message.value(QStringLiteral("requestId")).toString();
			const QString username = pendingProfiles_.take(requestId);
			auto player = players_.find(username);
			if (player != players_.end()) {
				const PlayerProfile profile = PlayerProfile::fromJson(
					message.value(QStringLiteral("profile")).toObject());
				player->displayName = profile.displayName;
				player->avatarUrl = profile.avatarUrl;
				player->countryCode = profile.countryCode;
				player->guessingScore = profile.guessingScore;
				player->drawingScore = profile.drawingScore;
				player->roundsWon = profile.roundsWon;
				player->roundsLost = profile.roundsLost;
				player->roundsAbandoned = profile.roundsAbandoned;
				player->leaderboardRank = profile.leaderboardRank;
				player->guessingLeaderboardRank = profile.guessingLeaderboardRank;
				player->drawingLeaderboardRank = profile.drawingLeaderboardRank;
				player->guessingRank = profile.guessingRank;
				player->drawingRank = profile.drawingRank;
				player->serverRole = profile.serverRole;
				player->masterSuperAdmin = profile.masterSuperAdmin;
				broadcastRoster();
			}
		} else if (type == QStringLiteral("ServerJoinCheckResult")) {
			const QString requestId = message.value(QStringLiteral("requestId")).toString();
			const auto pending = pendingJoinRequests_.find(requestId);
			if (pending == pendingJoinRequests_.end())
				continue;
			const PendingJoinRequest joinRequest = pending.value();
			pendingJoinRequests_.erase(pending);
			pendingJoinBySocket_.remove(joinRequest.socket);
			if (message.value(QStringLiteral("username")).toString().toLower() != joinRequest.username
				&& message.value(QStringLiteral("ok")).toBool()) {
				if (joinRequest.socket && joinRequest.socket->state() == QAbstractSocket::ConnectedState)
					sendMessage(joinRequest.socket, reply(QStringLiteral("JoinResult"), joinRequest.request,
						{{QStringLiteral("ok"), false},
						 {QStringLiteral("error"), QStringLiteral("Master authorization did not match this account")}}));
				continue;
			}
			completeJoinRoom(joinRequest, message);
		} else if (type == QStringLiteral("StaffRoleChanged")) {
			const QString username = message.value(QStringLiteral("username")).toString().toLower();
			auto player = players_.find(username);
			if (player != players_.end()) {
				player->serverRole = message.value(QStringLiteral("role")).toString(QStringLiteral("player"));
				player->masterSuperAdmin = message.value(QStringLiteral("masterSuperAdmin")).toBool();
				if (player->serverRole != QStringLiteral("player") || player->masterSuperAdmin) {
					for (Player& recipient : players_) {
						recipient.mutedTextUsers.remove(username);
						recipient.mutedVoiceUsers.remove(username);
					}
				}
				broadcastRoster();
			}
		} else if (type == QStringLiteral("PlayerMuteChanged")) {
			const QString muter = message.value(QStringLiteral("muterUsername")).toString().toLower();
			const QString target = message.value(QStringLiteral("targetUsername")).toString().toLower();
			auto player = players_.find(muter);
			if (player != players_.end()) {
				if (message.value(QStringLiteral("textChat")).toBool())
					player->mutedTextUsers.insert(target);
				else
					player->mutedTextUsers.remove(target);
				if (message.value(QStringLiteral("voiceChat")).toBool())
					player->mutedVoiceUsers.insert(target);
				else
					player->mutedVoiceUsers.remove(target);
			}
		} else if (type == QStringLiteral("ModerationApplied")) {
			const QString target = message.value(QStringLiteral("targetUsername")).toString().toLower();
			const QString action = message.value(QStringLiteral("action")).toString();
			auto player = players_.find(target);
			if (player == players_.end())
				continue;
			const QString reason = message.value(QStringLiteral("reason")).toString();
			if (action == QStringLiteral("kick") || action == QStringLiteral("ban")) {
				sendMessage(player->socket, makeMessage(QStringLiteral("ModerationNotice"),
					{{QStringLiteral("action"), action}, {QStringLiteral("reason"), reason}}));
				player->socket->disconnectFromHost();
			} else if (action == QStringLiteral("mute")) {
				const ServerSanction sanction = ServerSanction::fromJson(message.value(QStringLiteral("sanction")).toObject());
				player->serverTextMuted = sanction.textChat;
				player->serverVoiceMuted = sanction.voiceChat;
				player->serverMuteExpiresAt = sanction.expiresAt;
			} else if (action == QStringLiteral("unmute")) {
				player->serverTextMuted = false;
				player->serverVoiceMuted = false;
				player->serverMuteExpiresAt = 0;
			}
		} else if (type == QStringLiteral("RoundResultStored")) {
			const QString username = message.value(QStringLiteral("username")).toString();
			auto player = players_.find(username);
			if (player != players_.end()) {
				const PlayerProfile profile = PlayerProfile::fromJson(
					message.value(QStringLiteral("profile")).toObject());
				player->roundsWon = profile.roundsWon;
				player->roundsLost = profile.roundsLost;
				player->roundsAbandoned = profile.roundsAbandoned;
				player->countryCode = profile.countryCode;
				player->guessingScore = profile.guessingScore;
				player->drawingScore = profile.drawingScore;
				player->leaderboardRank = profile.leaderboardRank;
				player->guessingLeaderboardRank = profile.guessingLeaderboardRank;
				player->drawingLeaderboardRank = profile.drawingLeaderboardRank;
				player->guessingRank = profile.guessingRank;
				player->drawingRank = profile.drawingRank;
				broadcastRoster();
			}
		} else if (type == QStringLiteral("ScoreStored")) {
			const QString username = message.value(QStringLiteral("username")).toString();
			auto player = players_.find(username);
			if (player != players_.end()) {
				const PlayerProfile profile = PlayerProfile::fromJson(
					message.value(QStringLiteral("profile")).toObject());
				player->guessingScore = profile.guessingScore;
				player->drawingScore = profile.drawingScore;
				player->avatarUrl = profile.avatarUrl;
				player->countryCode = profile.countryCode;
				player->roundsWon = profile.roundsWon;
				player->roundsLost = profile.roundsLost;
				player->roundsAbandoned = profile.roundsAbandoned;
				player->leaderboardRank = profile.leaderboardRank;
				player->guessingLeaderboardRank = profile.guessingLeaderboardRank;
				player->drawingLeaderboardRank = profile.drawingLeaderboardRank;
				player->guessingRank = profile.guessingRank;
				player->drawingRank = profile.drawingRank;
				broadcastRoster();
			}
		} else if (type == QStringLiteral("RoundAbandonmentStored")) {
			const QString username = message.value(QStringLiteral("username")).toString();
			auto player = players_.find(username);
			if (player != players_.end()) {
				const PlayerProfile profile = PlayerProfile::fromJson(
					message.value(QStringLiteral("profile")).toObject());
				player->roundsAbandoned = profile.roundsAbandoned;
				broadcastRoster();
			}
		} else if (type == QStringLiteral("Error")) {
			qWarning() << "Master rejected game-server message:"
					   << message.value(QStringLiteral("message")).toString();
		}
	}
}

void GameServer::sendMasterRegistration(bool initial)
{
	QJsonArray rooms{currentRoomInfo().toJson()};
	QJsonObject message = makeMessage(initial ? QStringLiteral("ServerRegister")
											   : QStringLiteral("ServerHeartbeat"),
		{{QStringLiteral("serverId"), serverId_}, {QStringLiteral("host"), publicHost_},
		 {QStringLiteral("rooms"), rooms}});
	if (initial)
		message.insert(QStringLiteral("secret"), serverSecret_);
	sendMessage(&masterSocket_, message);
}

void GameServer::reportRoundResult(const QString& username, const QString& authToken, bool roundWon)
{
	if (!masterRegistered_)
		return;
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("RoundResult"),
		{{QStringLiteral("requestId"), QString::number(roundResultSequence_++)},
		 {QStringLiteral("authToken"), authToken},
		 {QStringLiteral("username"), username},
		 {QStringLiteral("roundWon"), roundWon}}));
}

void GameServer::reportRoundAbandonment(const QString& username, const QString& authToken)
{
	if (!masterRegistered_)
		return;
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("RoundAbandonment"),
		{{QStringLiteral("requestId"), QString::number(roundResultSequence_++)},
		 {QStringLiteral("authToken"), authToken},
		 {QStringLiteral("username"), username}}));
}

void GameServer::requestProfile(const Player& player)
{
	if (!masterRegistered_)
		return;
	const QString requestId = QString::number(nextMasterRequestId_++);
	pendingProfiles_.insert(requestId, player.username);
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("ProfileRequest"),
		{{QStringLiteral("requestId"), requestId}, {QStringLiteral("authToken"), player.authToken}}));
}

void GameServer::reportScore(const Player& player, int points, bool drawingPoints)
{
	if (!masterRegistered_)
		return;
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("ScoreUpdate"),
		{{QStringLiteral("requestId"), QString::number(scoreUpdateSequence_++)},
		 {QStringLiteral("username"), player.username}, {QStringLiteral("points"), points},
		 {QStringLiteral("drawingPoints"), drawingPoints}}));
}

void GameServer::handleCanvasOperation(QTcpSocket* socket, const QJsonObject& request)
{
	const QString username = playersBySocket_.value(socket);
	if (!roundActive_ || username != currentDrawer_) {
		sendMessage(socket, reply(QStringLiteral("Error"), request,
			{{QStringLiteral("message"), QStringLiteral("Only the current drawer can edit the canvas")}}));
		return;
	}
	QJsonObject normalized;
	QString error;
	if (!applyCanvasOperation(request.value(QStringLiteral("operation")).toObject(), &normalized, &error)) {
		sendMessage(socket, reply(QStringLiteral("Error"), request,
			{{QStringLiteral("message"), error}}));
		sendMessage(socket, makeMessage(QStringLiteral("CanvasState"),
			{{QStringLiteral("layers"), canvasLayersJson()},
			 {QStringLiteral("objects"), canvasObjectsJson()}}));
		return;
	}
	broadcast(makeMessage(QStringLiteral("CanvasOperation"),
		{{QStringLiteral("operation"), normalized}, {QStringLiteral("username"), username}}));
}

bool GameServer::applyCanvasOperation(const QJsonObject& operation, QJsonObject* normalized,
									  QString* error)
{
	const QString action = operation.value(QStringLiteral("action")).toString();
	if (action == QStringLiteral("object_upsert")) {
		const CanvasObject item = CanvasObject::fromJson(operation.value(QStringLiteral("object")).toObject());
		if (!isValidCanvasObject(item, error))
			return false;
		auto layer = std::find_if(canvasLayers_.begin(), canvasLayers_.end(), [&item](const CanvasLayer& value) {
			return value.id == item.layerId;
		});
		if (layer == canvasLayers_.end() || layer->locked) {
			if (error)
				*error = QStringLiteral("Target layer is missing or locked");
			return false;
		}
	auto existing = std::find_if(canvasObjects_.begin(), canvasObjects_.end(), [&item](const CanvasObject& value) {
			return value.id == item.id;
		});
		if (existing == canvasObjects_.end()) {
			if (canvasObjects_.size() >= MaximumCanvasObjects) {
				if (error)
					*error = QStringLiteral("Room canvas has reached its object limit");
				return false;
			}
			canvasObjects_.append(item);
		} else {
			auto oldLayer = std::find_if(canvasLayers_.cbegin(), canvasLayers_.cend(), [&existing](const CanvasLayer& value) {
				return value.id == existing->layerId;
			});
			if (oldLayer != canvasLayers_.cend() && oldLayer->locked) {
				if (error)
					*error = QStringLiteral("The object's current layer is locked");
				return false;
			}
			*existing = item;
		}
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("object"), item.toJson()}};
		return true;
	}
	if (action == QStringLiteral("object_delete")) {
		const QString id = operation.value(QStringLiteral("objectId")).toString();
		auto existing = std::find_if(canvasObjects_.begin(), canvasObjects_.end(), [&id](const CanvasObject& value) {
			return value.id == id;
		});
		if (existing == canvasObjects_.end()) {
			if (error)
				*error = QStringLiteral("Object not found");
			return false;
		}
		auto layer = std::find_if(canvasLayers_.cbegin(), canvasLayers_.cend(), [&existing](const CanvasLayer& value) {
			return value.id == existing->layerId;
		});
		if (layer != canvasLayers_.cend() && layer->locked) {
			if (error)
				*error = QStringLiteral("The object's layer is locked");
			return false;
		}
		canvasObjects_.erase(existing);
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("objectId"), id}};
		return true;
	}
	if (action == QStringLiteral("layer_upsert")) {
		const CanvasLayer layer = CanvasLayer::fromJson(operation.value(QStringLiteral("layer")).toObject());
		if (!isValidCanvasLayer(layer, error))
			return false;
		auto existing = std::find_if(canvasLayers_.begin(), canvasLayers_.end(), [&layer](const CanvasLayer& value) {
			return value.id == layer.id;
		});
		if (existing == canvasLayers_.end()) {
			if (canvasLayers_.size() >= MaximumCanvasLayers) {
				if (error)
					*error = QStringLiteral("Room canvas has reached its layer limit");
				return false;
			}
			canvasLayers_.append(layer);
		} else {
			*existing = layer;
		}
		std::stable_sort(canvasLayers_.begin(), canvasLayers_.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
			return left.order < right.order;
		});
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("layer"), layer.toJson()}};
		return true;
	}
	if (action == QStringLiteral("layer_reorder")) {
		const QJsonArray layerIds = operation.value(QStringLiteral("layerIds")).toArray();
		if (layerIds.size() != canvasLayers_.size()) {
			if (error)
				*error = QStringLiteral("Layer order does not match the room canvas");
			return false;
		}
		QSet<QString> requestedIds;
		QList<CanvasLayer> reorderedLayers;
		reorderedLayers.reserve(layerIds.size());
		for (const QJsonValue& value : layerIds) {
			if (!value.isString()) {
				if (error)
					*error = QStringLiteral("Invalid layer order");
				return false;
			}
			const QString id = value.toString();
			const auto existing = std::find_if(canvasLayers_.cbegin(), canvasLayers_.cend(), [&id](const CanvasLayer& layer) {
				return layer.id == id;
			});
			if (existing == canvasLayers_.cend() || requestedIds.contains(id)) {
				if (error)
					*error = QStringLiteral("Layer order must contain every room layer exactly once");
				return false;
			}
			requestedIds.insert(id);
			reorderedLayers.append(*existing);
		}
		for (int order = 0; order < reorderedLayers.size(); ++order)
			reorderedLayers[order].order = order;
		canvasLayers_ = reorderedLayers;
		QJsonArray normalizedIds;
		for (const CanvasLayer& layer : canvasLayers_)
			normalizedIds.append(layer.id);
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("layerIds"), QJsonValue(normalizedIds)}};
		return true;
	}
	if (action == QStringLiteral("layer_delete")) {
		const QString id = operation.value(QStringLiteral("layerId")).toString();
		auto layer = std::find_if(canvasLayers_.begin(), canvasLayers_.end(), [&id](const CanvasLayer& value) {
			return value.id == id;
		});
		if (layer == canvasLayers_.end() || canvasLayers_.size() <= 1 || layer->locked) {
			if (error)
				*error = QStringLiteral("Layer cannot be deleted");
			return false;
		}
		canvasLayers_.erase(layer);
		canvasObjects_.erase(std::remove_if(canvasObjects_.begin(), canvasObjects_.end(), [&id](const CanvasObject& item) {
			return item.layerId == id;
		}), canvasObjects_.end());
		for (int index = 0; index < canvasLayers_.size(); ++index)
			canvasLayers_[index].order = index;
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("layerId"), id}};
		return true;
	}
	if (action == QStringLiteral("clear_layer")) {
		const QString id = operation.value(QStringLiteral("layerId")).toString();
		auto layer = std::find_if(canvasLayers_.cbegin(), canvasLayers_.cend(), [&id](const CanvasLayer& value) {
			return value.id == id;
		});
		if (layer == canvasLayers_.cend() || layer->locked) {
			if (error)
				*error = QStringLiteral("Layer cannot be cleared");
			return false;
		}
		canvasObjects_.erase(std::remove_if(canvasObjects_.begin(), canvasObjects_.end(), [&id](const CanvasObject& item) {
			return item.layerId == id;
		}), canvasObjects_.end());
		*normalized = {{QStringLiteral("action"), action}, {QStringLiteral("layerId"), id}};
		return true;
	}
	if (action == QStringLiteral("clear")) {
		canvasObjects_.clear();
		*normalized = {{QStringLiteral("action"), action}};
		return true;
	}
	if (error)
		*error = QStringLiteral("Unsupported canvas operation");
	return false;
}

QJsonArray GameServer::canvasLayersJson() const
{
	QJsonArray result;
	for (const CanvasLayer& layer : canvasLayers_)
		result.append(layer.toJson());
	return result;
}

QJsonArray GameServer::canvasObjectsJson() const
{
	QJsonArray result;
	for (const CanvasObject& item : canvasObjects_)
		result.append(item.toJson());
	return result;
}

void GameServer::resetCanvas()
{
	canvasObjects_.clear();
	canvasLayers_ = {CanvasLayer{QStringLiteral("layer-default"), QStringLiteral("Background"), 0, true, false}};
}

void GameServer::startRound()
{
	if (activePlayerCount() < 2 || roundActive_ || roundStarting_)
		return;
	if (intermissionSeconds_ > 0) {
		roundStarting_ = true;
		intermissionDeadline_ = QDateTime::currentDateTime().addSecs(intermissionSeconds_);
		intermissionTimer_.start(intermissionSeconds_ * 1000);
		countdownTimer_.start();
		broadcast(makeMessage(QStringLiteral("RoundStarting"),
			{{QStringLiteral("secondsRemaining"), intermissionSeconds_}}));
		return;
	}
	startRoundNow();
}

void GameServer::startRoundNow()
{
	if (activePlayerCount() < 2 || roundActive_)
		return;
	QStringList activePlayers;
	for (const QString& username : playerOrder_) {
		const auto player = players_.constFind(username);
		if (player != players_.cend() && !player->spectator)
			activePlayers.append(username);
	}
	if (lastDrawer_.isEmpty()) {
		currentDrawer_ = activePlayers.first();
	} else {
		const int previousIndex = activePlayers.indexOf(lastDrawer_);
		currentDrawer_ = activePlayers.at(previousIndex >= 0 ? (previousIndex + 1) % activePlayers.size() : 0);
	}
	lastDrawer_ = currentDrawer_;
	roundWinnerUsername_.clear();
	roundParticipants_ = activePlayers;
	roundAbandonedParticipants_.clear();
	roundParticipantTokens_.clear();
	for (const QString& username : roundParticipants_)
		roundParticipantTokens_.insert(username, players_.value(username).authToken);
	currentWord_ = selectWord();
	++roundNumber_;
	correctGuesses_ = 0;
	for (auto player = players_.begin(); player != players_.end(); ++player)
		player->guessed = false;
	resetCanvas();
	roundActive_ = true;
	roundDeadline_ = QDateTime::currentDateTime().addSecs(room_.roundSeconds);
	roundTimer_.start(room_.roundSeconds * 1000);
	countdownTimer_.start();
	const QString drawerName = players_.value(currentDrawer_).displayName;
	logEvent(QStringLiteral("Round %1 started; %2 (@%3) is drawing")
		.arg(roundNumber_).arg(drawerName, currentDrawer_));

	broadcast(makeMessage(QStringLiteral("CanvasState"),
		{{QStringLiteral("layers"), canvasLayersJson()}, {QStringLiteral("objects"), canvasObjectsJson()}}));
	broadcast(makeMessage(QStringLiteral("RoundStarted"),
		{{QStringLiteral("round"), roundNumber_}, {QStringLiteral("drawerUsername"), currentDrawer_},
		 {QStringLiteral("roundSeconds"), room_.roundSeconds},
		 {QStringLiteral("secondsRemaining"), room_.roundSeconds}}));
	sendMessage(players_.value(currentDrawer_).socket,
				makeMessage(QStringLiteral("WordAssigned"), {{QStringLiteral("word"), currentWord_}}));
}

void GameServer::cancelIntermissionIfInsufficientPlayers()
{
	if (!roundStarting_ || activePlayerCount() >= 2)
		return;
	roundStarting_ = false;
	intermissionTimer_.stop();
	countdownTimer_.stop();
	broadcast(makeMessage(QStringLiteral("RoundPaused"),
		{{QStringLiteral("message"), QStringLiteral("Waiting for another player")}}));
}

void GameServer::endRound(const QString& reason)
{
	if (!roundActive_)
		return;
	roundActive_ = false;
	roundTimer_.stop();
	countdownTimer_.stop();
	if (reason != QStringLiteral("drawer_left")) {
		for (const QString& username : roundParticipants_) {
			if (roundAbandonedParticipants_.contains(username))
				continue;
			reportRoundResult(username, roundParticipantTokens_.value(username), username == roundWinnerUsername_);
		}
	}
	roundWinnerUsername_.clear();
	roundParticipants_.clear();
	roundAbandonedParticipants_.clear();
	roundParticipantTokens_.clear();
	broadcast(makeMessage(QStringLiteral("RoundEnded"),
		{{QStringLiteral("round"), roundNumber_}, {QStringLiteral("word"), currentWord_},
		 {QStringLiteral("reason"), reason}}));
	currentWord_.clear();
	currentDrawer_.clear();
	updateMasterRoom();
	if (activePlayerCount() >= 2) {
		QTimer::singleShot(3000, this, [this] {
			if (!roundActive_ && activePlayerCount() >= 2)
				startRound();
		});
	}
}

void GameServer::markRoundAbandoned(const QString& username)
{
	if (!roundActive_ || !roundParticipantTokens_.contains(username)
		|| roundAbandonedParticipants_.contains(username))
		return;
	roundAbandonedParticipants_.insert(username);
	reportRoundAbandonment(username, roundParticipantTokens_.value(username));
}

void GameServer::sendGuess(const QString& username, const QString& rawText)
{
	auto guesser = players_.find(username);
	if (!roundActive_ || guesser == players_.end() || guesser->spectator
		|| username == currentDrawer_ || guesser->guessed)
		return;
	const QString text = rawText.trimmed();
	if (!isValidSingleLineText(text, 100))
		return;
	if (text.compare(currentWord_, Qt::CaseInsensitive) != 0) {
		broadcast(makeMessage(QStringLiteral("ChatMessage"),
			{{QStringLiteral("username"), username}, {QStringLiteral("displayName"), guesser->displayName},
			 {QStringLiteral("text"), text}, {QStringLiteral("correct"), false}}));
		return;
	}

	guesser->guessed = true;
	++correctGuesses_;
	const int placement = correctGuesses_;
	if (placement == 1)
		roundWinnerUsername_ = username;
	logEvent(QStringLiteral("%1 (@%2) guessed the answer in place %3")
		.arg(guesser->displayName, username).arg(placement));
		const int points = pointsForCorrectGuessPlacement(placement);
	guesser->roomScore += points;
	guesser->guessingScore += points;
	reportScore(guesser.value(), points, false);

	int drawerBonus = 0;
	auto drawer = players_.find(currentDrawer_);
	if (drawer != players_.end()) {
		drawerBonus = pointsForDrawerPerCorrectGuess();
		drawer->roomScore += drawerBonus;
		drawer->drawingScore += drawerBonus;
		reportScore(drawer.value(), drawerBonus, true);
	}

	broadcast(makeMessage(QStringLiteral("GuessCorrect"),
		{{QStringLiteral("username"), username}, {QStringLiteral("displayName"), guesser->displayName},
		 {QStringLiteral("placement"), placement}, {QStringLiteral("points"), points},
		 {QStringLiteral("drawerBonus"), drawerBonus}}));
	broadcastRoster();
	if (correctGuesses_ >= activePlayerCount() - 1)
		endRound(QStringLiteral("all_guessed"));
}

bool GameServer::validStroke(const QJsonObject& object, DrawingStroke* stroke) const
{
	if (!stroke)
		return false;
	const QJsonArray points = object.value(QStringLiteral("points")).toArray();
	if (points.isEmpty() || points.size() > 500)
		return false;
	const QString color = object.value(QStringLiteral("color")).toString(QStringLiteral("#202124"));
	static const QRegularExpression colorPattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	if (!colorPattern.match(color).hasMatch())
		return false;
	for (const QJsonValue& point : points) {
		if (!point.isObject())
			return false;
		const QJsonObject coordinates = point.toObject();
		if (!coordinates.value(QStringLiteral("x")).isDouble()
			|| !coordinates.value(QStringLiteral("y")).isDouble())
			return false;
		const double x = coordinates.value(QStringLiteral("x")).toDouble(-1.0);
		const double y = coordinates.value(QStringLiteral("y")).toDouble(-1.0);
		if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
			return false;
	}
	stroke->color = color;
	stroke->width = qBound(1, object.value(QStringLiteral("width")).toInt(4), 32);
	stroke->points = points;
	return true;
}

QString GameServer::selectWord() const
{
	return words_.at(QRandomGenerator::global()->bounded(static_cast<int>(words_.size())));
}

bool GameServer::loadDictionary(QString* error)
{
	QFile dictionary(dictionaryPath_);
	if (!dictionary.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral("Unable to open dictionary '%1': %2").arg(dictionaryPath_, dictionary.errorString());
		return false;
	}
	QStringList loadedWords;
	while (!dictionary.atEnd()) {
		const QString word = QString::fromUtf8(dictionary.readLine()).trimmed();
		if (word.isEmpty() || word.size() > 40 || !isValidSingleLineText(word, 40))
			continue;
		loadedWords.append(word);
	}
	if (loadedWords.isEmpty()) {
		if (error)
			*error = QStringLiteral("Dictionary '%1' contains no usable words").arg(dictionaryPath_);
		return false;
	}
	words_ = std::move(loadedWords);
	return true;
}

}
