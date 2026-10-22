#pragma once

#include "storage/account_store.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QJsonObject>
#include <QSet>

class QTcpSocket;

namespace gtd {

class MasterServer final : public QObject {
public:
	explicit MasterServer(AccountStore* accountStore, QString serverSecret, QString sessionSecret,
						  int superAdminRefreshSeconds = 1, QObject* parent = nullptr,
						  QString configFilePath = {});
	~MasterServer() override;
	bool start(const QHostAddress& address, quint16 port);
	QHostAddress serverAddress() const;
	quint16 serverPort() const;

private:
	struct Registration {
		QTcpSocket* socket = nullptr;
		QList<RoomInfo> rooms;
		qint64 lastHeartbeat = 0;
	};

	void acceptConnections();
	void readSocket(QTcpSocket* socket);
	void handleMessage(QTcpSocket* socket, const QJsonObject& message);
	void handleClientMessage(QTcpSocket* socket, const QJsonObject& message);
	void handleServerMessage(QTcpSocket* socket, const QJsonObject& message);
	void handleServerRegistration(QTcpSocket* socket, const QJsonObject& message);
	void updateRegistration(QTcpSocket* socket, const QJsonObject& message, bool initial);
	void logPopulationStats();
	void removeSocket(QTcpSocket* socket, bool logDisconnect = true);
	void expireServers();
	void refreshMasterSuperAdmins();
	void refreshRuntimeConfiguration();
	void sendRoomList(QTcpSocket* socket, const QJsonObject& request);
	void sendAuthResult(QTcpSocket* socket, const QJsonObject& request,
						const std::optional<PlayerProfile>& profile, const QString& error,
						bool replaceExistingSession = false);
	void sendError(QTcpSocket* socket, const QJsonObject& request, const QString& message);
	bool verifyClientSession(const QJsonObject& request, QString* username, QString* error) const;
	UserDirectoryEntry directoryEntry(const QString& viewer, const PlayerProfile& profile, QString* error);
	QJsonObject presenceData(const QString& username) const;
	void broadcastPresence(const QString& username);
	void clearPresenceForServer(const QString& serverId);

	QTcpServer listener_;
	QTimer expiryTimer_;
	QTimer superAdminRefreshTimer_;
	AccountStore* accountStore_;
	QString serverSecret_;
	QString sessionSecret_;
	QString configFilePath_;
	int automaticBanDistinctReports_ = 3;
	int automaticBanDurationValue_ = 1;
	QString automaticBanDurationUnit_ = QStringLiteral("hours");
	QTimer configurationRefreshTimer_;
	QHash<QTcpSocket*, QByteArray> pendingData_;
	QHash<QTcpSocket*, QString> serverIdsBySocket_;
	QHash<QTcpSocket*, QString> authenticatedClients_;
	QHash<QTcpSocket*, quint64> lastRoundResultSequence_;
	QHash<QTcpSocket*, quint64> lastScoreUpdateSequence_;
	QHash<QString, Registration> registrations_;
	QHash<QString, QJsonObject> roomPresenceByUsername_;
	QSet<QString> masterSuperAdmins_;
	qint64 masterSuperAdminDataVersion_ = -1;
	int lastRoomCount_ = -1;
	int lastConnectedCount_ = -1;
	int lastPlayingCount_ = -1;
	int lastSpectatingCount_ = -1;
	int lastIdleCount_ = -1;
};

}
