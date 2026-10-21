#include "master/master_server.h"

#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QTcpSocket>
#include <QHostAddress>

#include <algorithm>
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

constexpr qint64 ServerTimeoutSeconds = 15;
constexpr qint64 SessionLifetimeSeconds = 24 * 60 * 60;
constexpr qsizetype MaximumRoomsPerServer = 128;

bool validServerId(const QString& serverId)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
	return pattern.match(serverId).hasMatch();
}

int roleLevel(const QString& role, bool superAdmin)
{
	if (superAdmin)
		return 3;
	if (role == QStringLiteral("admin"))
		return 2;
	if (role == QStringLiteral("moderator"))
		return 1;
	return 0;
}

QString effectiveRole(AccountStore* store, const QString& serverId, const QString& username,
	bool* superAdmin, QString* error)
{
	const bool isSuperAdmin = store->isMasterSuperAdmin(username, error);
	if (error && !error->isEmpty())
		return {};
	if (superAdmin)
		*superAdmin = isSuperAdmin;
	return store->serverRole(serverId, username, error);
}

QJsonArray sanctionArray(const QList<ServerSanction>& sanctions, const QString& kind = {})
{
	QJsonArray result;
	for (const ServerSanction& sanction : sanctions) {
		if (kind.isEmpty() || sanction.kind == kind)
			result.append(sanction.toJson());
	}
	return result;
}

bool constantTimeEqual(const QByteArray& left, const QByteArray& right)
{
	if (left.size() != right.size())
		return false;
	uchar difference = 0;
	for (qsizetype index = 0; index < left.size(); ++index)
		difference |= static_cast<uchar>(left.at(index) ^ right.at(index));
	return difference == 0;
}

QJsonObject withRequestId(const QString& type, const QJsonObject& request, const QJsonObject& fields = {})
{
	QJsonObject response = makeMessage(type, fields);
	const QJsonValue requestId = request.value(QStringLiteral("requestId"));
	if (!requestId.isUndefined())
		response.insert(QStringLiteral("requestId"), requestId);
	return response;
}

void writePopulationStatus(const QString& status)
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

MasterServer::MasterServer(AccountStore* accountStore, QString serverSecret, QString sessionSecret,
						   int superAdminRefreshSeconds, QObject* parent)
	: QObject(parent),
	  accountStore_(accountStore),
	  serverSecret_(std::move(serverSecret)),
	  sessionSecret_(std::move(sessionSecret))
{
	connect(&listener_, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
	expiryTimer_.setInterval(5000);
	connect(&expiryTimer_, &QTimer::timeout, this, [this] { expireServers(); });
	expiryTimer_.start();
	superAdminRefreshTimer_.setInterval(qBound(1, superAdminRefreshSeconds, 3600) * 1000);
	connect(&superAdminRefreshTimer_, &QTimer::timeout, this, [this] { refreshMasterSuperAdmins(); });
}

MasterServer::~MasterServer()
{
	listener_.close();
	const auto sockets = listener_.findChildren<QTcpSocket*>();
	for (QTcpSocket* socket : sockets) {
		disconnect(socket, nullptr, this, nullptr);
		socket->abort();
	}
}

bool MasterServer::start(const QHostAddress& address, quint16 port)
{
	refreshMasterSuperAdmins();
	if (!listener_.listen(address, port))
		return false;
	superAdminRefreshTimer_.start();
	QTimer::singleShot(0, this, [this] { logPopulationStats(); });
	return true;
}

void MasterServer::refreshMasterSuperAdmins()
{
	QString error;
	const qint64 dataVersion = accountStore_->masterSuperAdminDataVersion(&error);
	if (dataVersion < 0) {
		qWarning() << "Could not check master super-admin database version:" << error;
		return;
	}
	if (masterSuperAdminDataVersion_ >= 0 && dataVersion == masterSuperAdminDataVersion_)
		return;
	const QList<QString> names = accountStore_->masterSuperAdmins(&error);
	if (!error.isEmpty()) {
		qWarning() << "Could not refresh master super-admin accounts:" << error;
		return;
	}
	QSet<QString> updated;
	for (const QString& username : names)
		updated.insert(username.toLower());
	if (masterSuperAdminDataVersion_ >= 0 && updated != masterSuperAdmins_) {
		QSet<QString> changed = updated;
		changed.unite(masterSuperAdmins_);
		for (const QString& username : std::as_const(changed)) {
			if (updated.contains(username) == masterSuperAdmins_.contains(username))
				continue;
			for (auto registration = registrations_.begin(); registration != registrations_.end(); ++registration) {
				const QString serverRole = accountStore_->serverRole(registration.key(), username, &error);
				if (!error.isEmpty()) {
					qWarning() << "Could not refresh server role for" << username << error;
					return;
				}
				sendMessage(registration->socket, makeMessage(QStringLiteral("StaffRoleChanged"),
					{{QStringLiteral("serverId"), registration.key()}, {QStringLiteral("username"), username},
					 {QStringLiteral("role"), serverRole},
					 {QStringLiteral("masterSuperAdmin"), updated.contains(username)}}));
			}
		}
		qInfo() << "Applied master super-admin database changes without restart";
	}
	masterSuperAdmins_ = std::move(updated);
	masterSuperAdminDataVersion_ = dataVersion;
}

QHostAddress MasterServer::serverAddress() const
{
	return listener_.serverAddress();
}

quint16 MasterServer::serverPort() const
{
	return listener_.serverPort();
}

void MasterServer::acceptConnections()
{
	while (listener_.hasPendingConnections()) {
		QTcpSocket* socket = listener_.nextPendingConnection();
		socket->setReadBufferSize(MaximumFrameBytes);
		pendingData_.insert(socket, {});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readSocket(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			removeSocket(socket);
			socket->deleteLater();
		});
	}
}

void MasterServer::readSocket(QTcpSocket* socket)
{
	QString error;
	const QList<QJsonObject> messages = takeMessages(&pendingData_[socket], socket->readAll(), &error);
	if (!error.isEmpty()) {
		sendError(socket, {}, error);
		socket->disconnectFromHost();
		return;
	}
	for (const QJsonObject& message : messages)
		handleMessage(socket, message);
}

void MasterServer::handleMessage(QTcpSocket* socket, const QJsonObject& message)
{
	if (serverIdsBySocket_.contains(socket)) {
		handleServerMessage(socket, message);
		return;
	}
	if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ServerRegister")) {
		handleServerRegistration(socket, message);
		return;
	}
	handleClientMessage(socket, message);
}

void MasterServer::handleClientMessage(QTcpSocket* socket, const QJsonObject& message)
{
	const QString type = message.value(QStringLiteral("type")).toString();
	const QString username = message.value(QStringLiteral("username")).toString();
	QString error;

	if (type == QStringLiteral("Logout")) {
		if (authenticatedClients_.remove(socket) > 0)
			logPopulationStats();
		return;
	}
	if (type == QStringLiteral("ServerAdminDataRequest")
		|| type == QStringLiteral("StaffRoleRequest")
		|| type == QStringLiteral("ModerationRequest")
		|| type == QStringLiteral("PlayerMuteRequest")
		|| type == QStringLiteral("PlayerMuteListRequest")
		|| type == QStringLiteral("PlayerProfileRequest")) {
		QString actor;
		if (!verifyClientSession(message, &actor, &error)) {
			sendError(socket, message, error);
			return;
		}
		const QString serverId = message.value(QStringLiteral("serverId")).toString();
		if (!validServerId(serverId)) {
			sendError(socket, message, QStringLiteral("Invalid server ID"));
			return;
		}
		if (!registrations_.contains(serverId)) {
			sendError(socket, message, QStringLiteral("Server is unavailable"));
			return;
		}
		bool isSuperAdmin = false;
		const QString actorRole = effectiveRole(accountStore_, serverId, actor, &isSuperAdmin, &error);
		if (!error.isEmpty()) {
			sendError(socket, message, error);
			return;
		}
		const int actorLevel = roleLevel(actorRole, isSuperAdmin);
		if (type == QStringLiteral("ServerAdminDataRequest")) {
			if (actorLevel < 1 || !registrations_.contains(serverId)) {
				sendError(socket, message, QStringLiteral("You cannot manage this server"));
				return;
			}
			const QList<ServerRoleAssignment> staff = accountStore_->serverStaff(serverId, &error);
			const QList<ServerSanction> sanctions = accountStore_->activeServerSanctions(serverId, {}, -1, &error);
			if (!error.isEmpty()) {
				sendError(socket, message, error);
				return;
			}
			QJsonArray staffJson;
			for (const ServerRoleAssignment& assignment : staff)
				staffJson.append(assignment.toJson());
			sendMessage(socket, withRequestId(QStringLiteral("ServerAdminData"), message,
				{{QStringLiteral("serverId"), serverId}, {QStringLiteral("staff"), staffJson},
				 {QStringLiteral("bans"), sanctionArray(sanctions, QStringLiteral("ban"))},
				 {QStringLiteral("mutes"), sanctionArray(sanctions, QStringLiteral("mute"))}}));
			return;
		}
		if (type == QStringLiteral("PlayerProfileRequest")) {
			const QString target = message.value(QStringLiteral("username")).toString().trimmed().toLower();
			auto profile = accountStore_->profile(target, &error);
			if (!profile) {
				sendError(socket, message, error);
				return;
			}
			profile->serverRole = accountStore_->serverRole(serverId, target, &error);
			profile->masterSuperAdmin = accountStore_->isMasterSuperAdmin(target, &error);
			if (!error.isEmpty()) {
				sendError(socket, message, error);
				return;
			}
			sendMessage(socket, withRequestId(QStringLiteral("PlayerProfileResult"), message,
				{{QStringLiteral("profile"), profile->toJson()}}));
			return;
		}
		if (type == QStringLiteral("PlayerMuteListRequest")) {
			const QList<PlayerMutePreference> preferences = accountStore_->playerMutePreferences(serverId, actor, &error);
			QJsonArray preferenceArray;
			for (const PlayerMutePreference& preference : preferences) {
				const QString role = accountStore_->serverRole(serverId, preference.targetUsername, &error);
				const bool targetIsSuperAdmin = accountStore_->isMasterSuperAdmin(preference.targetUsername, &error);
				if (!error.isEmpty())
					break;
				if (role == QStringLiteral("player") && !targetIsSuperAdmin)
					preferenceArray.append(preference.toJson());
			}
			if (!error.isEmpty()) {
				sendError(socket, message, error);
				return;
			}
			sendMessage(socket, withRequestId(QStringLiteral("PlayerMuteList"), message,
				{{QStringLiteral("preferences"), preferenceArray}}));
			return;
		}
		const QString target = message.value(QStringLiteral("targetUsername")).toString().trimmed().toLower();
		if (target.isEmpty() || !accountStore_->profile(target, &error)) {
			sendError(socket, message, error.isEmpty() ? QStringLiteral("Player not found") : error);
			return;
		}
		const QString targetRole = effectiveRole(accountStore_, serverId, target, nullptr, &error);
		const bool targetSuperAdmin = accountStore_->isMasterSuperAdmin(target, &error);
		if (!error.isEmpty()) {
			sendError(socket, message, error);
			return;
		}
		const int targetLevel = roleLevel(targetRole, targetSuperAdmin);
		if (type == QStringLiteral("StaffRoleRequest")) {
			const QString newRole = message.value(QStringLiteral("role")).toString();
			const bool validRole = newRole == QStringLiteral("admin")
				|| newRole == QStringLiteral("moderator") || newRole == QStringLiteral("player");
			const bool allowed = isSuperAdmin
				? validRole
				: actorRole == QStringLiteral("admin") && newRole != QStringLiteral("admin")
					&& !targetSuperAdmin && targetRole != QStringLiteral("admin");
			if (!validRole || !allowed || !registrations_.contains(serverId)) {
				sendError(socket, message, QStringLiteral("You cannot assign that server role"));
				return;
			}
			if (!accountStore_->setServerRole(serverId, target, newRole, actor, &error)) {
				sendError(socket, message, error);
				return;
			}
			if ((newRole != QStringLiteral("player") || targetSuperAdmin)
				&& !accountStore_->clearPlayerMutePreferencesForTarget(serverId, target, &error)) {
				sendError(socket, message, error);
				return;
			}
			const QJsonObject update = makeMessage(QStringLiteral("StaffRoleChanged"),
				{{QStringLiteral("serverId"), serverId}, {QStringLiteral("username"), target},
				 {QStringLiteral("role"), newRole}, {QStringLiteral("masterSuperAdmin"), targetSuperAdmin}});
			sendMessage(registrations_.value(serverId).socket, update);
			sendMessage(socket, withRequestId(QStringLiteral("StaffRoleResult"), message,
				{{QStringLiteral("ok"), true}, {QStringLiteral("username"), target},
				 {QStringLiteral("role"), newRole}}));
			return;
		}
		if (type == QStringLiteral("PlayerMuteRequest")) {
			if (targetLevel > 0) {
				sendError(socket, message, QStringLiteral("Staff members cannot be muted by players"));
				return;
			}
			const bool textChat = message.value(QStringLiteral("textChat")).toBool();
			const bool voiceChat = message.value(QStringLiteral("voiceChat")).toBool();
			if (!accountStore_->setPlayerMute(serverId, actor, target, textChat, voiceChat, &error)) {
				sendError(socket, message, error);
				return;
			}
			const QJsonObject update = makeMessage(QStringLiteral("PlayerMuteChanged"),
				{{QStringLiteral("serverId"), serverId}, {QStringLiteral("muterUsername"), actor},
				 {QStringLiteral("targetUsername"), target}, {QStringLiteral("textChat"), textChat},
				 {QStringLiteral("voiceChat"), voiceChat}});
			sendMessage(registrations_.value(serverId).socket, update);
			sendMessage(socket, withRequestId(QStringLiteral("PlayerMuteResult"), message,
				{{QStringLiteral("ok"), true}, {QStringLiteral("targetUsername"), target},
				 {QStringLiteral("textChat"), textChat}, {QStringLiteral("voiceChat"), voiceChat}}));
			return;
		}
		const QString action = message.value(QStringLiteral("action")).toString();
		if (!registrations_.contains(serverId) || action.isEmpty()) {
			sendError(socket, message, QStringLiteral("Server is unavailable or action is invalid"));
			return;
		}
		if (!isSuperAdmin && actorLevel <= targetLevel) {
			sendError(socket, message, QStringLiteral("You cannot moderate this player"));
			return;
		}
		QJsonObject updateFields{{QStringLiteral("serverId"), serverId},
			{QStringLiteral("targetUsername"), target}, {QStringLiteral("issuedBy"), actor},
			{QStringLiteral("action"), action}};
		if (action == QStringLiteral("kick")) {
			const QString reason = message.value(QStringLiteral("reason")).toString().trimmed();
			if (!isValidSingleLineText(reason, 500)) {
				sendError(socket, message, QStringLiteral("A reason of up to 500 characters is required"));
				return;
			}
			updateFields.insert(QStringLiteral("reason"), reason);
		} else if (action == QStringLiteral("ban") || action == QStringLiteral("mute")) {
			ServerSanction sanction;
			sanction.serverId = serverId;
			sanction.targetUsername = target;
			sanction.kind = action;
			sanction.textChat = message.value(QStringLiteral("textChat")).toBool();
			sanction.voiceChat = message.value(QStringLiteral("voiceChat")).toBool();
			sanction.reason = message.value(QStringLiteral("reason")).toString();
			sanction.issuedBy = actor;
			const QJsonValue expiresAt = message.value(QStringLiteral("expiresAt"));
			if (!expiresAt.isUndefined() && !expiresAt.isNull()) {
				if (!expiresAt.isDouble()) {
					sendError(socket, message, QStringLiteral("Expiration must be a UTC Unix timestamp or null"));
					return;
				}
				sanction.expiresAt = static_cast<qint64>(expiresAt.toDouble());
			}
			if (action == QStringLiteral("ban"))
				sanction.textChat = sanction.voiceChat = false;
			qint64 sanctionId = 0;
			if (!accountStore_->addServerSanction(sanction, &sanctionId, &error)) {
				sendError(socket, message, error);
				return;
			}
			const auto records = accountStore_->activeServerSanctions(serverId, target, -1, &error);
			for (const ServerSanction& record : records) {
				if (record.id == sanctionId) {
					updateFields.insert(QStringLiteral("sanction"), record.toJson());
					break;
				}
			}
		} else if (action == QStringLiteral("unban") || action == QStringLiteral("unmute")) {
			const qint64 sanctionId = static_cast<qint64>(message.value(QStringLiteral("sanctionId")).toDouble());
			const auto records = accountStore_->activeServerSanctions(serverId, target, -1, &error);
			const auto found = std::find_if(records.cbegin(), records.cend(),
				[sanctionId, &action](const ServerSanction& item) {
					return item.id == sanctionId && item.kind == (action == QStringLiteral("unban")
						? QStringLiteral("ban") : QStringLiteral("mute"));
				});
			if (found == records.cend() || !accountStore_->revokeServerSanction(serverId, sanctionId, actor, &error)) {
				sendError(socket, message, error.isEmpty() ? QStringLiteral("Active sanction not found") : error);
				return;
			}
			updateFields.insert(QStringLiteral("sanction"), found->toJson());
		} else {
			sendError(socket, message, QStringLiteral("Unknown moderation action"));
			return;
		}
		sendMessage(registrations_.value(serverId).socket,
			makeMessage(QStringLiteral("ModerationApplied"), updateFields));
		sendMessage(socket, withRequestId(QStringLiteral("ModerationResult"), message,
			{{QStringLiteral("ok"), true}, {QStringLiteral("action"), action},
			 {QStringLiteral("targetUsername"), target}}));
		return;
	}
	if (type == QStringLiteral("CreateAccount")) {
		if (!accountStore_->createAccount(username, message.value(QStringLiteral("password")).toString(),
										  message.value(QStringLiteral("displayName")).toString(), &error)) {
			sendAuthResult(socket, message, std::nullopt, error);
			return;
		}
		const auto profile = accountStore_->profile(username, &error);
		sendAuthResult(socket, message, profile, error);
		return;
	}
	if (type == QStringLiteral("Login")) {
		const auto profile = accountStore_->authenticate(username,
			message.value(QStringLiteral("password")).toString(), &error);
		sendAuthResult(socket, message, profile, error, true);
		return;
	}
	if (type == QStringLiteral("ListRooms")) {
		const QString authToken = message.value(QStringLiteral("authToken")).toString();
		if (!authToken.isEmpty()) {
			QString tokenUsername;
			if (!verifySessionToken(authToken, sessionSecret_, &tokenUsername)) {
				sendError(socket, message, QStringLiteral("Session expired or invalid"));
				return;
			}
			authenticatedClients_.insert(socket, tokenUsername);
			logPopulationStats();
		}
		sendRoomList(socket, message);
		return;
	}
	if (type == QStringLiteral("ProfileRequest")) {
		QString tokenUsername;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
								&tokenUsername)) {
			sendError(socket, message, QStringLiteral("Session expired or invalid"));
			return;
		}
		const auto profile = accountStore_->profile(tokenUsername, &error);
		if (!profile) {
			sendError(socket, message, error);
			return;
		}
		sendMessage(socket, withRequestId(QStringLiteral("ProfileResult"), message,
										  {{QStringLiteral("profile"), profile->toJson()}}));
		return;
	}
	if (type == QStringLiteral("UpdateCountry")) {
		QString tokenUsername;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
								&tokenUsername)) {
			sendError(socket, message, QStringLiteral("Session expired or invalid"));
			return;
		}
		if (!accountStore_->updateCountry(tokenUsername,
										  message.value(QStringLiteral("countryCode")).toString(), &error)) {
			sendError(socket, message, error);
			return;
		}
		const auto profile = accountStore_->profile(tokenUsername, &error);
		if (!profile) {
			sendError(socket, message, error);
			return;
		}
		sendMessage(socket, withRequestId(QStringLiteral("CountryUpdated"), message,
										  {{QStringLiteral("profile"), profile->toJson()}}));
		return;
	}
	if (type == QStringLiteral("UpdateProfile")) {
		QString tokenUsername;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
								&tokenUsername)) {
			sendError(socket, message, QStringLiteral("Session expired or invalid"));
			return;
		}
		if (!accountStore_->updateProfile(tokenUsername,
			message.value(QStringLiteral("displayName")).toString(),
			message.value(QStringLiteral("countryCode")).toString(),
			message.value(QStringLiteral("currentPassword")).toString(),
			message.value(QStringLiteral("newPassword")).toString(), &error)) {
			sendError(socket, message, error);
			return;
		}
		const auto profile = accountStore_->profile(tokenUsername, &error);
		if (!profile) {
			sendError(socket, message, error);
			return;
		}
		sendMessage(socket, withRequestId(QStringLiteral("ProfileUpdated"), message,
			{{QStringLiteral("profile"), profile->toJson()}}));
		return;
	}
	if (type == QStringLiteral("LeaderboardRequest")) {
		QString tokenUsername;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
								&tokenUsername)) {
			sendError(socket, message, QStringLiteral("Session expired or invalid"));
			return;
		}
		const QString track = message.value(QStringLiteral("track")).toString();
		if (track != QStringLiteral("guessing") && track != QStringLiteral("drawing")) {
			sendError(socket, message, QStringLiteral("Invalid leaderboard track"));
			return;
		}
		const bool drawingPoints = track == QStringLiteral("drawing");
		const QList<PlayerProfile> profiles = accountStore_->topPlayers(drawingPoints, 1000, &error);
		if (!error.isEmpty()) {
			sendError(socket, message, error);
			return;
		}
		QJsonArray players;
		for (const PlayerProfile& profile : profiles)
			players.append(profile.toJson());
		sendMessage(socket, withRequestId(QStringLiteral("LeaderboardResult"), message,
										  {{QStringLiteral("track"), track},
										   {QStringLiteral("players"), players}}));
		return;
	}
	sendError(socket, message, QStringLiteral("Unknown master request"));
}

bool MasterServer::verifyClientSession(const QJsonObject& request, QString* username, QString* error) const
{
	if (!verifySessionToken(request.value(QStringLiteral("authToken")).toString(), sessionSecret_, username)) {
		if (error)
			*error = QStringLiteral("Session expired or invalid");
		return false;
	}
	return true;
}

void MasterServer::sendAuthResult(QTcpSocket* socket, const QJsonObject& request,
								  const std::optional<PlayerProfile>& profile, const QString& error,
								  bool replaceExistingSession)
{
	if (!profile) {
		sendMessage(socket, withRequestId(QStringLiteral("AuthResult"), request,
			{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}));
		return;
	}
	if (replaceExistingSession) {
		for (auto client = authenticatedClients_.begin(); client != authenticatedClients_.end();) {
			if (client.key() == socket || client.value() != profile->username) {
				++client;
				continue;
			}
			QTcpSocket* previousSocket = client.key();
			client = authenticatedClients_.erase(client);
			sendMessage(previousSocket, makeMessage(QStringLiteral("SessionReplaced")));
			previousSocket->disconnectFromHost();
		}
	}
	authenticatedClients_.insert(socket, profile->username);
	logPopulationStats();
	const qint64 expiresAt = QDateTime::currentSecsSinceEpoch() + SessionLifetimeSeconds;
	const QString token = createSessionToken(profile->username, profile->displayName, sessionSecret_, expiresAt);
	sendMessage(socket, withRequestId(QStringLiteral("AuthResult"), request,
		{{QStringLiteral("ok"), true}, {QStringLiteral("authToken"), token},
		 {QStringLiteral("expiresAt"), expiresAt}, {QStringLiteral("profile"), profile->toJson()}}));
}

void MasterServer::sendRoomList(QTcpSocket* socket, const QJsonObject& request)
{
	QJsonArray rooms;
	for (auto registration = registrations_.cbegin(); registration != registrations_.cend(); ++registration) {
		for (const RoomInfo& room : registration->rooms)
			rooms.append(room.toJson());
	}
	sendMessage(socket, withRequestId(QStringLiteral("RoomList"), request,
									  {{QStringLiteral("rooms"), rooms}}));
}

void MasterServer::handleServerRegistration(QTcpSocket* socket, const QJsonObject& message)
{
	const QByteArray suppliedSecret = message.value(QStringLiteral("secret")).toString().toUtf8();
	if (serverSecret_.isEmpty() || !constantTimeEqual(suppliedSecret, serverSecret_.toUtf8())) {
		sendError(socket, message, QStringLiteral("Game server registration rejected"));
		socket->disconnectFromHost();
		return;
	}
	if (!message.value(QStringLiteral("rooms")).isArray()) {
		sendError(socket, message, QStringLiteral("Initial room list is required"));
		socket->disconnectFromHost();
		return;
	}
	if (message.value(QStringLiteral("rooms")).toArray().size() > MaximumRoomsPerServer) {
		sendError(socket, message, QStringLiteral("Game server advertises too many rooms"));
		socket->disconnectFromHost();
		return;
	}

	const QString serverId = message.value(QStringLiteral("serverId")).toString().trimmed();
	static const QRegularExpression serverIdPattern(QStringLiteral("^[A-Za-z0-9_-]{1,48}$"));
	if (!serverIdPattern.match(serverId).hasMatch()) {
		sendError(socket, message, QStringLiteral("Invalid server ID"));
		socket->disconnectFromHost();
		return;
	}

	auto existing = registrations_.find(serverId);
	if (existing != registrations_.end() && existing->socket != socket) {
		QTcpSocket* oldSocket = existing->socket;
		qInfo().noquote() << QStringLiteral("Replacing connection for game server %1").arg(serverId);
		serverIdsBySocket_.remove(oldSocket);
		existing->socket = nullptr;
		oldSocket->disconnectFromHost();
	}

	serverIdsBySocket_.insert(socket, serverId);
	updateRegistration(socket, message, true);
	sendMessage(socket, withRequestId(QStringLiteral("ServerRegisterAck"), message,
									  {{QStringLiteral("ok"), true}}));
	qInfo().noquote() << QStringLiteral("Game server registered: %1 at %2 with %3 room(s)")
		.arg(serverId, socket->peerAddress().toString())
		.arg(static_cast<int>(registrations_.value(serverId).rooms.size()));
}

void MasterServer::handleServerMessage(QTcpSocket* socket, const QJsonObject& message)
{
	const QString type = message.value(QStringLiteral("type")).toString();
	if (type == QStringLiteral("ServerJoinCheck")) {
		const QString serverId = serverIdsBySocket_.value(socket);
		QString username;
		QString error;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_, &username)) {
			sendMessage(socket, withRequestId(QStringLiteral("ServerJoinCheckResult"), message,
				{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Session expired or invalid")}}));
			return;
		}
		auto profile = accountStore_->profile(username, &error);
		const QList<ServerSanction> sanctions = accountStore_->activeServerSanctions(serverId, username, -1, &error);
		const QList<PlayerMutePreference> preferences = accountStore_->playerMutePreferences(serverId, username, &error);
		if (!profile || !error.isEmpty()) {
			sendMessage(socket, withRequestId(QStringLiteral("ServerJoinCheckResult"), message,
				{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.isEmpty()
					? QStringLiteral("Account profile not found") : error}}));
			return;
		}
		profile->serverRole = accountStore_->serverRole(serverId, username, &error);
		profile->masterSuperAdmin = accountStore_->isMasterSuperAdmin(username, &error);
		if (!error.isEmpty()) {
			sendMessage(socket, withRequestId(QStringLiteral("ServerJoinCheckResult"), message,
				{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}));
			return;
		}
		const auto ban = std::find_if(sanctions.cbegin(), sanctions.cend(), [](const ServerSanction& sanction) {
			return sanction.kind == QStringLiteral("ban");
		});
		QJsonArray preferenceArray;
		for (const PlayerMutePreference& preference : preferences) {
			const QString mutedRole = accountStore_->serverRole(serverId, preference.targetUsername, &error);
			const bool mutedSuperAdmin = accountStore_->isMasterSuperAdmin(preference.targetUsername, &error);
			if (!error.isEmpty())
				break;
			if (mutedRole == QStringLiteral("player") && !mutedSuperAdmin)
				preferenceArray.append(preference.toJson());
		}
		if (!error.isEmpty()) {
			sendMessage(socket, withRequestId(QStringLiteral("ServerJoinCheckResult"), message,
				{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}));
			return;
		}
		const bool allowed = ban == sanctions.cend();
		QJsonObject fields{{QStringLiteral("ok"), allowed}, {QStringLiteral("username"), username},
			{QStringLiteral("profile"), profile->toJson()},
			{QStringLiteral("sanctions"), sanctionArray(sanctions, QStringLiteral("mute"))},
			{QStringLiteral("playerMutes"), preferenceArray}};
		if (!allowed) {
			fields.insert(QStringLiteral("error"), QStringLiteral("Banned: %1").arg(ban->reason));
			fields.insert(QStringLiteral("ban"), ban->toJson());
		}
		sendMessage(socket, withRequestId(QStringLiteral("ServerJoinCheckResult"), message, fields));
	} else if (type == QStringLiteral("ServerHeartbeat")) {
		updateRegistration(socket, message, false);
		sendMessage(socket, withRequestId(QStringLiteral("ServerHeartbeatAck"), message,
										  {{QStringLiteral("ok"), true}}));
	} else if (type == QStringLiteral("ServerUnregister")) {
		qInfo().noquote() << QStringLiteral("Game server unregistered: %1")
			.arg(serverIdsBySocket_.value(socket));
		removeSocket(socket, false);
		sendMessage(socket, withRequestId(QStringLiteral("ServerUnregisterAck"), message,
										  {{QStringLiteral("ok"), true}}));
		socket->disconnectFromHost();
	} else if (type == QStringLiteral("ServerEvent")) {
		QString event = message.value(QStringLiteral("message")).toString().left(512).trimmed();
		event.replace(QLatin1Char('\n'), QLatin1Char(' '));
		event.replace(QLatin1Char('\r'), QLatin1Char(' '));
		event.replace(QLatin1Char('\t'), QLatin1Char(' '));
		if (!event.isEmpty()) {
			qInfo().noquote() << QStringLiteral("Game server %1: %2")
				.arg(serverIdsBySocket_.value(socket), event);
		}
	} else if (type == QStringLiteral("ScoreUpdate")) {
		const QString username = message.value(QStringLiteral("username")).toString();
		const int points = message.value(QStringLiteral("points")).toInt(-1);
		const QJsonValue drawingPointsValue = message.value(QStringLiteral("drawingPoints"));
		const bool drawingPoints = drawingPointsValue.toBool();
		bool sequenceOk = false;
		const quint64 sequence = message.value(QStringLiteral("requestId")).toString().toULongLong(&sequenceOk);
		const bool validPointAward = points == 2 || points == 5 || points == 7 || points == 10;
		if (!sequenceOk || sequence == 0 || sequence <= lastScoreUpdateSequence_.value(socket, 0)
			|| !message.value(QStringLiteral("points")).isDouble() || !drawingPointsValue.isBool()
			|| username.isEmpty() || !validPointAward || (drawingPoints && points != 2)) {
			sendError(socket, message, QStringLiteral("Invalid or replayed score update"));
			return;
		}
		QString error;
		if (!accountStore_->addScore(username, points, drawingPoints, &error)) {
			sendError(socket, message, error);
			return;
		}
		lastScoreUpdateSequence_.insert(socket, sequence);
		const auto profile = accountStore_->profile(username, &error);
		sendMessage(socket, withRequestId(QStringLiteral("ScoreStored"), message,
			{{QStringLiteral("username"), username},
			 {QStringLiteral("profile"), profile ? profile->toJson() : QJsonObject{}}}));
	} else if (type == QStringLiteral("RoundResult")
		|| type == QStringLiteral("RoundAbandonment")) {
		const bool abandoned = type == QStringLiteral("RoundAbandonment");
		QString username;
		QString error;
		bool sequenceOk = false;
		const quint64 sequence = message.value(QStringLiteral("requestId")).toString().toULongLong(&sequenceOk);
		if (!sequenceOk || sequence == 0 || !verifySessionToken(message.value(QStringLiteral("authToken")).toString(),
													 sessionSecret_, &username)) {
			sendError(socket, message, QStringLiteral("Invalid round result authorization"));
			return;
		}
		if (message.value(QStringLiteral("username")).toString() != username) {
			sendError(socket, message, QStringLiteral("Round result account does not match its session"));
			return;
		}
		if ((!abandoned && !message.value(QStringLiteral("roundWon")).isBool())
			|| sequence <= lastRoundResultSequence_.value(socket, 0)) {
			sendError(socket, message, QStringLiteral("Invalid or replayed round result"));
			return;
		}
		const bool recorded = abandoned
			? accountStore_->recordRoundAbandonment(username, &error)
			: accountStore_->recordRoundResult(username, message.value(QStringLiteral("roundWon")).toBool(), &error);
		if (!recorded) {
			sendError(socket, message, error);
			return;
		}
		lastRoundResultSequence_.insert(socket, sequence);
		const auto profile = accountStore_->profile(username, &error);
		sendMessage(socket, withRequestId(abandoned ? QStringLiteral("RoundAbandonmentStored")
															 : QStringLiteral("RoundResultStored"), message,
			{{QStringLiteral("username"), username},
			 {QStringLiteral("profile"), profile ? profile->toJson() : QJsonObject{}}}));
	} else if (type == QStringLiteral("ProfileRequest")) {
		QString username;
		QString error;
		if (!verifySessionToken(message.value(QStringLiteral("authToken")).toString(), sessionSecret_,
								&username)) {
			sendError(socket, message, QStringLiteral("Session expired or invalid"));
			return;
		}
		auto profile = accountStore_->profile(username, &error);
		if (!profile) {
			sendError(socket, message, error);
			return;
		}
		profile->serverRole = accountStore_->serverRole(serverIdsBySocket_.value(socket), username, &error);
		profile->masterSuperAdmin = accountStore_->isMasterSuperAdmin(username, &error);
		if (!error.isEmpty()) {
			sendError(socket, message, error);
			return;
		}
		sendMessage(socket, withRequestId(QStringLiteral("ProfileResult"), message,
										  {{QStringLiteral("profile"), profile->toJson()}}));
	} else {
		sendError(socket, message, QStringLiteral("Unknown game-server message"));
	}
}

void MasterServer::updateRegistration(QTcpSocket* socket, const QJsonObject& message, bool initial)
{
	const QString serverId = serverIdsBySocket_.value(socket);
	if (serverId.isEmpty())
		return;
	Registration& registration = registrations_[serverId];
	registration.socket = socket;
	registration.lastHeartbeat = QDateTime::currentSecsSinceEpoch();

	const QJsonValue roomsValue = message.value(QStringLiteral("rooms"));
	if (!roomsValue.isArray() && initial)
		return;
	if (roomsValue.isArray()) {
		const QJsonArray roomValues = roomsValue.toArray();
		if (roomValues.size() > MaximumRoomsPerServer)
			return;
		QList<RoomInfo> rooms;
		QSet<QString> roomIds;
		const QString advertisedHost = message.value(QStringLiteral("host")).toString().trimmed();
		QString host = advertisedHost;
		QHostAddress hostAddress;
		static const QRegularExpression hostnamePattern(QStringLiteral(
			"^(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(?:\\.(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?))*$"));
		if (host.isEmpty() || host.size() > 253
			|| (!hostAddress.setAddress(host) && !hostnamePattern.match(host).hasMatch()))
			host = socket->peerAddress().toString();
		static const QRegularExpression roomIdPattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
		for (const QJsonValue& value : roomValues) {
			if (!value.isObject())
				continue;
			const QJsonObject roomObject = value.toObject();
			const QJsonValue portValue = roomObject.value(QStringLiteral("port"));
			const int port = portValue.toInt(-1);
			const QJsonValue maxPlayersValue = roomObject.value(QStringLiteral("maxPlayers"));
			const int maxPlayers = maxPlayersValue.toInt(-1);
			const QJsonValue roundSecondsValue = roomObject.value(QStringLiteral("roundSeconds"));
			const int roundSeconds = roundSecondsValue.toInt(-1);
			RoomInfo room = RoomInfo::fromJson(roomObject);
			if (!roomIdPattern.match(room.roomId).hasMatch()
				|| !isValidSingleLineText(room.roomName, 64)
				|| !portValue.isDouble() || port < 1 || port > 65535
				|| !maxPlayersValue.isDouble() || maxPlayers < 2 || maxPlayers > 32
				|| !roundSecondsValue.isDouble() || roundSeconds < 10 || roundSeconds > 600
				|| roomIds.contains(room.roomId))
				continue;
			room.serverId = serverId;
			room.host = host;
			roomIds.insert(room.roomId);
			rooms.append(room);
		}
		registration.rooms = rooms;
		logPopulationStats();
	}
}

void MasterServer::logPopulationStats()
{
	int playing = 0;
	int spectating = 0;
	int roomCount = 0;
	for (const Registration& registration : registrations_) {
		roomCount += static_cast<int>(registration.rooms.size());
		for (const RoomInfo& room : registration.rooms) {
			playing += qMax(0, room.players);
			spectating += qMax(0, room.spectators);
		}
	}
	const int inRooms = playing + spectating;
	const int connected = qMax(static_cast<int>(authenticatedClients_.size()), inRooms);
	const int idle = connected - inRooms;
	if (roomCount == lastRoomCount_ && connected == lastConnectedCount_ && playing == lastPlayingCount_
		&& spectating == lastSpectatingCount_ && idle == lastIdleCount_)
		return;
	lastRoomCount_ = roomCount;
	lastConnectedCount_ = connected;
	lastPlayingCount_ = playing;
	lastSpectatingCount_ = spectating;
	lastIdleCount_ = idle;
	writePopulationStatus(QStringLiteral("Rooms: %1 | Players online: %2 | Playing: %3 | Spectating: %4 | Idle: %5")
		.arg(roomCount).arg(connected).arg(playing).arg(spectating).arg(idle));
}

void MasterServer::removeSocket(QTcpSocket* socket, bool logDisconnect)
{
	pendingData_.remove(socket);
	const bool clientRemoved = authenticatedClients_.remove(socket) > 0;
	lastRoundResultSequence_.remove(socket);
	lastScoreUpdateSequence_.remove(socket);
	const QString serverId = serverIdsBySocket_.take(socket);
	if (serverId.isEmpty()) {
		if (clientRemoved)
			logPopulationStats();
		return;
	}
	if (logDisconnect)
		qInfo().noquote() << QStringLiteral("Game server disconnected: %1").arg(serverId);
	const auto registration = registrations_.find(serverId);
	if (registration != registrations_.end() && registration->socket == socket)
		registrations_.erase(registration);
	logPopulationStats();
}

void MasterServer::expireServers()
{
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	bool expiredAny = false;
	for (auto registration = registrations_.begin(); registration != registrations_.end();) {
		if (now - registration->lastHeartbeat > ServerTimeoutSeconds) {
			expiredAny = true;
			qWarning().noquote() << QStringLiteral("Game server heartbeat timed out: %1")
				.arg(registration.key());
			QTcpSocket* socket = registration->socket;
			serverIdsBySocket_.remove(socket);
			registration = registrations_.erase(registration);
			if (socket && socket->state() != QAbstractSocket::UnconnectedState)
				socket->disconnectFromHost();
		} else {
			++registration;
		}
	}
	if (expiredAny)
		logPopulationStats();
}

void MasterServer::sendError(QTcpSocket* socket, const QJsonObject& request, const QString& message)
{
	sendMessage(socket, withRequestId(QStringLiteral("Error"), request,
									  {{QStringLiteral("message"), message}}));
}

}
