#pragma once

#include "shared/models.h"

#include <QDateTime>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>

namespace gtd {

class GameServer final : public QObject {
public:
	GameServer(RoomInfo room, QString serverId, QString publicHost, QString masterHost,
			   quint16 masterPort, QString serverSecret, QString sessionSecret,
			   QString dictionaryPath = {}, int intermissionSeconds = 15, QObject* parent = nullptr,
			   QByteArray roomPasskeySalt = {}, QByteArray roomPasskeyHash = {});
	~GameServer() override;
	bool start(QString* error = nullptr);
	quint16 port() const;

private:
	struct Player {
		QTcpSocket* socket = nullptr;
		QString username;
		QString displayName;
		QString avatarUrl;
		QString countryCode;
		QString serverRole = QStringLiteral("player");
		QString authToken;
		int roomScore = 0;
		int guessingScore = 0;
		int drawingScore = 0;
		int roundsWon = 0;
		int roundsLost = 0;
		int roundsAbandoned = 0;
		int leaderboardRank = 0;
		int guessingLeaderboardRank = 0;
		int drawingLeaderboardRank = 0;
		QString guessingRank;
		QString drawingRank;
		bool guessed = false;
		bool spectator = false;
		bool queuedForPlayer = false;
		bool masterSuperAdmin = false;
		bool serverTextMuted = false;
		bool serverVoiceMuted = false;
		qint64 serverMuteExpiresAt = 0;
		QSet<QString> mutedTextUsers;
		QSet<QString> mutedVoiceUsers;
	};
	struct RoomPasskeyChallenge {
		QString requestId;
		QByteArray nonce;
		qint64 expiresAtMs = 0;
	};
	struct PendingJoinRequest {
		QTcpSocket* socket = nullptr;
		QJsonObject request;
		QString username;
		QString displayName;
	};
	struct ActionWindow {
		qint64 startedAtMs = -1;
		int count = 0;
	};
	struct SocketRateLimit {
		QHash<QString, ActionWindow> actions;
		qint64 strikeWindowStartedAtMs = -1;
		int strikes = 0;
	};

	void acceptConnections();
	void readPlayer(QTcpSocket* socket);
	bool allowAction(QTcpSocket* socket, const QString& type);
	void handlePlayerMessage(QTcpSocket* socket, const QJsonObject& message);
	void joinRoom(QTcpSocket* socket, const QJsonObject& message);
	void completeJoinRoom(const PendingJoinRequest& pending, const QJsonObject& authorization);
	void promoteToPlayer(QTcpSocket* socket, const QJsonObject& request);
	void changePlayerRole(QTcpSocket* socket, const QJsonObject& request);
	void enqueueForPlayer(const QString& username);
	void removeFromPlayerQueue(const QString& username);
	void broadcastQueueStatus();
	void processPlayerQueue();
	QJsonObject queueStatus(const QString& username) const;
	void removePlayer(QTcpSocket* socket);
	int activePlayerCount() const;
	int spectatorCount() const;
	RoomInfo currentRoomInfo() const;
	void sendRoomSnapshot(QTcpSocket* socket);
	QJsonArray roster() const;
	void broadcast(const QJsonObject& message);
	void broadcastRoster();
	void updateMasterRoom();
	void updateConsoleStatus();
	void connectToMaster();
	void logEvent(const QString& message);
	void readMaster();
	void sendMasterRegistration(bool initial);
	void requestProfile(const Player& player);
	void reportScore(const Player& player, int points, bool drawingPoints);
	void reportRoundResult(const QString& username, const QString& authToken, bool roundWon);
	void reportRoundAbandonment(const QString& username, const QString& authToken);
	void markRoundAbandoned(const QString& username);
	void handleCanvasOperation(QTcpSocket* socket, const QJsonObject& request);
	bool applyCanvasOperation(const QJsonObject& operation, QJsonObject* normalized,
							 QString* error);
	QJsonArray canvasLayersJson() const;
	QJsonArray canvasObjectsJson() const;
	void resetCanvas();
	void startRound();
	void startRoundNow();
	void cancelIntermissionIfInsufficientPlayers();
	void endRound(const QString& reason);
	void sendGuess(const QString& username, const QString& text);
	bool validStroke(const QJsonObject& object, DrawingStroke* stroke) const;
	QString selectWord() const;
	bool loadDictionary(QString* error);

	RoomInfo room_;
	QString serverId_;
	QString publicHost_;
	QString masterHost_;
	quint16 masterPort_ = 0;
	QString serverSecret_;
	QString sessionSecret_;
	QByteArray roomPasskeySalt_;
	QByteArray roomPasskeyHash_;
	QString dictionaryPath_;
	int intermissionSeconds_ = 15;
	QStringList words_;
	QTcpServer listener_;
	QTcpSocket masterSocket_;
	QByteArray masterBuffer_;
	QTimer heartbeatTimer_;
	QTimer reconnectTimer_;
	QTimer roundTimer_;
	QTimer intermissionTimer_;
	QTimer countdownTimer_;
	QElapsedTimer actionRateLimitClock_;
	QHash<QTcpSocket*, QByteArray> playerBuffers_;
	QHash<QTcpSocket*, SocketRateLimit> socketRateLimits_;
	QHash<QTcpSocket*, QString> playersBySocket_;
	QHash<QString, Player> players_;
	QHash<QString, PendingJoinRequest> pendingJoinRequests_;
	QHash<QTcpSocket*, QString> pendingJoinBySocket_;
	QHash<QTcpSocket*, RoomPasskeyChallenge> roomPasskeyChallenges_;
	QHash<QString, QString> pendingProfiles_;
	QStringList playerOrder_;
	QStringList playerQueue_;
	QStringList roundParticipants_;
	QSet<QString> roundAbandonedParticipants_;
	QHash<QString, QString> roundParticipantTokens_;
	QList<CanvasLayer> canvasLayers_;
	QList<CanvasObject> canvasObjects_;
	QString currentDrawer_;
	QString roundWinnerUsername_;
	QString lastDrawer_;
	QString currentWord_;
	QDateTime roundDeadline_;
	QDateTime intermissionDeadline_;
	int roundNumber_ = 0;
	int correctGuesses_ = 0;
	quint64 roundResultSequence_ = 1;
	quint64 scoreUpdateSequence_ = 1;
	int lastStatusPlayerCount_ = -1;
	int lastStatusSpectatorCount_ = -1;
	int lastStatusQueueCount_ = -1;
	quint64 nextMasterRequestId_ = 1;
	bool roundActive_ = false;
	bool roundStarting_ = false;
	bool masterRegistered_ = false;
};

}
