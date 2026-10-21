#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace gtd {

struct RoomInfo {
	QString serverId;
	QString roomId;
	QString roomName;
	QString host;
	quint16 port = 0;
	int players = 0;
	int spectators = 0;
	int maxPlayers = 8;
	int roundSeconds = 60;

	QJsonObject toJson() const;
	static RoomInfo fromJson(const QJsonObject& object);
};

struct PlayerProfile {
	QString username;
	QString displayName;
	QString avatarUrl;
	QString countryCode;
	int totalScore = 0;
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
	QString serverRole = QStringLiteral("player");
	bool masterSuperAdmin = false;

	QJsonObject toJson() const;
	static PlayerProfile fromJson(const QJsonObject& object);
};

struct ServerRoleAssignment {
	QString serverId;
	QString username;
	QString role;
	QString assignedBy;

	QJsonObject toJson() const;
	static ServerRoleAssignment fromJson(const QJsonObject& object);
};

struct ServerSanction {
	qint64 id = 0;
	QString serverId;
	QString targetUsername;
	QString kind;
	bool textChat = false;
	bool voiceChat = false;
	QString reason;
	QString issuedBy;
	qint64 createdAt = 0;
	qint64 expiresAt = 0;

	QJsonObject toJson() const;
	static ServerSanction fromJson(const QJsonObject& object);
};

struct PlayerMutePreference {
	QString serverId;
	QString muterUsername;
	QString targetUsername;
	bool textChat = false;
	bool voiceChat = false;

	QJsonObject toJson() const;
	static PlayerMutePreference fromJson(const QJsonObject& object);
};

struct DrawingStroke {
	QString color = QStringLiteral("#202124");
	int width = 4;
	QJsonArray points;

	QJsonObject toJson() const;
	static DrawingStroke fromJson(const QJsonObject& object);
};

inline constexpr int MaximumCanvasLayers = 32;
inline constexpr int MaximumCanvasObjects = 1000;
inline constexpr int MaximumObjectPoints = 2000;
inline constexpr qsizetype MaximumCanvasImageDataLength = 700 * 1024;
inline constexpr int MaximumCanvasImageDimension = 512;

struct CanvasLayer {
	QString id;
	QString name;
	int order = 0;
	bool visible = true;
	bool locked = false;

	QJsonObject toJson() const;
	static CanvasLayer fromJson(const QJsonObject& object);
};

struct CanvasObject {
	QString id;
	QString layerId;
	QString type;
	QString color = QStringLiteral("#202124");
	QString fillColor = QStringLiteral("#ffffff");
	QString text;
	QString imageData;
	QJsonArray points;
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;
	int strokeWidth = 4;
	int fontSize = 24;
	bool filled = false;

	QJsonObject toJson() const;
	static CanvasObject fromJson(const QJsonObject& object);
};

bool isValidCanvasLayer(const CanvasLayer& layer, QString* error = nullptr);
bool isValidCanvasObject(const CanvasObject& object, QString* error = nullptr);
bool isValidSingleLineText(const QString& text, qsizetype maximumLength);

}
