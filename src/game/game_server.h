#pragma once

#include "shared/models.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>

namespace gtd {

class GameServer final : public QObject {
public:
	GameServer(RoomInfo room, QString serverId, QString publicHost, QString masterHost,
			   quint16 masterPort, QString serverSecret, QString sessionSecret,
			   QObject* parent = nullptr);
	~GameServer() override;
	bool start(QString* error = nullptr);
	quint16 port() const;

private:
	struct Player {
		QTcpSocket* socket = nullptr;
		QString username;
		QString displayName;
		QString avatarUrl;
		QString authToken;
		int roomScore = 0;
		int guessingScore = 0;
		int drawingScore = 0;
		int roundsWon = 0;
		int roundsLost = 0;
		int leaderboardRank = 0;
		int guessingLeaderboardRank = 0;
		int drawingLeaderboardRank = 0;
		QString guessingRank;
		QString drawingRank;
		bool guessed = false;
		bool spectator = false;
		bool queuedForPlayer = false;
	};

	void acceptConnections();
	void readPlayer(QTcpSocket* socket);
	void handlePlayerMessage(QTcpSocket* socket, const QJsonObject& message);
	void joinRoom(QTcpSocket* socket, const QJsonObject& message);
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
	void handleCanvasOperation(QTcpSocket* socket, const QJsonObject& request);
	bool applyCanvasOperation(const QJsonObject& operation, QJsonObject* normalized,
							 QString* error);
	QJsonArray canvasLayersJson() const;
	QJsonArray canvasObjectsJson() const;
	void resetCanvas();
	void startRound();
	void endRound(const QString& reason);
	void sendGuess(const QString& username, const QString& text);
	bool validStroke(const QJsonObject& object, DrawingStroke* stroke) const;
	QString selectWord() const;

	RoomInfo room_;
	QString serverId_;
	QString publicHost_;
	QString masterHost_;
	quint16 masterPort_ = 0;
	QString serverSecret_;
	QString sessionSecret_;
	QTcpServer listener_;
	QTcpSocket masterSocket_;
	QByteArray masterBuffer_;
	QTimer heartbeatTimer_;
	QTimer reconnectTimer_;
	QTimer roundTimer_;
	QTimer countdownTimer_;
	QHash<QTcpSocket*, QByteArray> playerBuffers_;
	QHash<QTcpSocket*, QString> playersBySocket_;
	QHash<QString, Player> players_;
	QHash<QString, QString> pendingProfiles_;
	QStringList playerOrder_;
	QStringList playerQueue_;
	QStringList roundParticipants_;
	QHash<QString, QString> roundParticipantTokens_;
	QList<CanvasLayer> canvasLayers_;
	QList<CanvasObject> canvasObjects_;
	QString currentDrawer_;
	QString roundWinnerUsername_;
	QString lastDrawer_;
	QString currentWord_;
	QDateTime roundDeadline_;
	int roundNumber_ = 0;
	int correctGuesses_ = 0;
	quint64 roundResultSequence_ = 1;
	int lastStatusPlayerCount_ = -1;
	int lastStatusSpectatorCount_ = -1;
	int lastStatusQueueCount_ = -1;
	quint64 nextMasterRequestId_ = 1;
	bool roundActive_ = false;
	bool masterRegistered_ = false;
};

}
