#pragma once

#include "shared/models.h"

#include <QSqlDatabase>
#include <QList>
#include <QString>
#include <optional>

namespace gtd {

class AccountStore {
public:
	explicit AccountStore(const QString& databasePath);
	~AccountStore();

	AccountStore(const AccountStore&) = delete;
	AccountStore& operator=(const AccountStore&) = delete;

	bool initialize(QString* error = nullptr);
	bool createAccount(const QString& username, const QString& password,
					   const QString& displayName, QString* error = nullptr);
	std::optional<PlayerProfile> authenticate(const QString& username, const QString& password,
											   QString* error = nullptr);
	std::optional<PlayerProfile> profile(const QString& username, QString* error = nullptr);
	QList<PlayerProfile> searchProfiles(const QString& text, int limit = 50, QString* error = nullptr);
	QList<ServerSanction> activeBansForUser(const QString& username, QString* error = nullptr);
	QList<QString> friends(const QString& username, QString* error = nullptr);
	QList<FriendRequest> friendRequests(const QString& username, QString* error = nullptr);
	QList<QString> blockedUsers(const QString& username, QString* error = nullptr);
	QList<QString> inboxContacts(const QString& username, QString* error = nullptr);
	bool deleteInboxConversation(const QString& username, const QString& otherUsername, QString* error = nullptr);
	bool deleteDirectMessage(const QString& username, qint64 messageId, QString* error = nullptr);
	int unreadDirectMessageCount(const QString& username, const QString& otherUsername, QString* error = nullptr);
	bool markDirectMessagesRead(const QString& username, const QString& otherUsername, QString* error = nullptr);
	bool areFriends(const QString& firstUsername, const QString& secondUsername, QString* error = nullptr);
	bool isBlocked(const QString& firstUsername, const QString& secondUsername, QString* error = nullptr);
	bool sendFriendRequest(const QString& senderUsername, const QString& recipientUsername,
		qint64* requestId = nullptr, QString* error = nullptr);
	bool acceptFriendRequest(const QString& recipientUsername, qint64 requestId, QString* error = nullptr);
	bool denyFriendRequest(const QString& recipientUsername, qint64 requestId, QString* error = nullptr);
	bool clearDeniedFriendRequest(const QString& senderUsername, qint64 requestId, QString* error = nullptr);
	bool removeFriend(const QString& username, const QString& friendUsername, QString* error = nullptr);
	bool setBlocked(const QString& blockerUsername, const QString& targetUsername, bool blocked,
		QString* error = nullptr);
	bool sendDirectMessage(const QString& senderUsername, const QString& recipientUsername,
		const QString& body, qint64* messageId = nullptr, QString* error = nullptr);
	QList<DirectMessage> directMessageHistory(const QString& username, const QString& otherUsername,
		qint64 beforeId = 0, int limit = 100, QString* error = nullptr);
	bool updateCountry(const QString& username, const QString& countryCode, QString* error = nullptr);
	bool updateProfile(const QString& username, const QString& displayName, const QString& countryCode,
		const QString& currentPassword, const QString& newPassword, QString* error = nullptr);
	QList<PlayerProfile> topPlayers(bool drawingPoints, int limit = 1000, QString* error = nullptr);
	bool addScore(const QString& username, int points, bool drawingPoints, QString* error = nullptr);
	bool recordRoundResult(const QString& username, bool roundWon, QString* error = nullptr);
	bool recordRoundAbandonment(const QString& username, QString* error = nullptr);
	bool isMasterSuperAdmin(const QString& username, QString* error = nullptr);
	QList<QString> masterSuperAdmins(QString* error = nullptr);
	qint64 masterSuperAdminDataVersion(QString* error = nullptr);
	QString serverRole(const QString& serverId, const QString& username, QString* error = nullptr);
	QList<ServerRoleAssignment> serverStaff(const QString& serverId, QString* error = nullptr);
	bool setServerRole(const QString& serverId, const QString& username, const QString& role,
		const QString& assignedBy, QString* error = nullptr);
	bool addServerSanction(const ServerSanction& sanction, qint64* sanctionId = nullptr,
		QString* error = nullptr);
	QList<ServerSanction> activeServerSanctions(const QString& serverId,
		const QString& targetUsername = {}, qint64 now = -1, QString* error = nullptr);
	bool revokeServerSanction(const QString& serverId, qint64 sanctionId,
		const QString& revokedBy, QString* error = nullptr);
	bool setPlayerMute(const QString& serverId, const QString& muterUsername,
		const QString& targetUsername, bool textChat, bool voiceChat, QString* error = nullptr);
	QList<PlayerMutePreference> playerMutePreferences(const QString& serverId,
		const QString& muterUsername, QString* error = nullptr);
	bool clearPlayerMutePreferencesForTarget(const QString& serverId,
		const QString& targetUsername, QString* error = nullptr);
	bool reportPlayer(const QString& serverId, const QString& reporterUsername, const QString& targetUsername,
		const QString& reason, int* recentReportCount = nullptr, QString* error = nullptr);

private:
	std::optional<PlayerProfile> loadProfile(const QString& normalizedUsername, QString* error);
	void addProfileRank(PlayerProfile* profile);
	QString databasePath_;
	QString connectionName_;
	QSqlDatabase database_;
};

}
