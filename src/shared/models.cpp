#include "shared/models.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>

namespace gtd {
namespace {

void setValidationError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

bool validId(const QString& id)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
	return pattern.match(id).hasMatch();
}

bool validColor(const QString& color)
{
	static const QRegularExpression pattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	return pattern.match(color).hasMatch();
}

bool validPoints(const QJsonArray& points)
{
	if (points.size() > MaximumObjectPoints)
		return false;
	for (const QJsonValue& value : points) {
		if (!value.isObject())
			return false;
		const QJsonObject point = value.toObject();
		if (!point.value(QStringLiteral("x")).isDouble()
			|| !point.value(QStringLiteral("y")).isDouble())
			return false;
		const double x = point.value(QStringLiteral("x")).toDouble(-1.0);
		const double y = point.value(QStringLiteral("y")).toDouble(-1.0);
		if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
			return false;
	}
	return true;
}

bool validCanvasImageData(const QString& imageData)
{
	if (imageData.isEmpty() || imageData.size() > MaximumCanvasImageDataLength)
		return false;
	const QByteArray encoded = imageData.toLatin1();
	if (QString::fromLatin1(encoded) != imageData)
		return false;
	const QByteArray decoded = QByteArray::fromBase64(encoded);
	if (decoded.toBase64() != encoded || decoded.size() < 24
		|| decoded.left(8) != QByteArray::fromHex("89504e470d0a1a0a")
		|| decoded.mid(12, 4) != QByteArrayLiteral("IHDR"))
		return false;
	const auto* header = reinterpret_cast<const uchar*>(decoded.constData() + 16);
	const quint32 width = qFromBigEndian<quint32>(header);
	const quint32 height = qFromBigEndian<quint32>(header + 4);
	return width > 0 && height > 0 && width <= MaximumCanvasImageDimension
		&& height <= MaximumCanvasImageDimension;
}

}

QJsonObject RoomInfo::toJson() const
{
	return {{QStringLiteral("serverId"), serverId},
			{QStringLiteral("roomId"), roomId},
			{QStringLiteral("roomName"), roomName},
			{QStringLiteral("host"), host},
			{QStringLiteral("port"), static_cast<int>(port)},
			{QStringLiteral("players"), players},
			{QStringLiteral("spectators"), spectators},
			{QStringLiteral("maxPlayers"), maxPlayers},
			{QStringLiteral("roundSeconds"), roundSeconds}};
}

RoomInfo RoomInfo::fromJson(const QJsonObject& object)
{
	RoomInfo room;
	room.serverId = object.value(QStringLiteral("serverId")).toString();
	room.roomId = object.value(QStringLiteral("roomId")).toString();
	room.roomName = object.value(QStringLiteral("roomName")).toString();
	room.host = object.value(QStringLiteral("host")).toString();
	room.port = static_cast<quint16>(qBound(0, object.value(QStringLiteral("port")).toInt(), 65535));
	room.players = qMax(0, object.value(QStringLiteral("players")).toInt());
	room.spectators = qMax(0, object.value(QStringLiteral("spectators")).toInt());
	room.maxPlayers = qBound(1, object.value(QStringLiteral("maxPlayers")).toInt(8), 32);
	room.roundSeconds = qBound(10, object.value(QStringLiteral("roundSeconds")).toInt(60), 600);
	return room;
}

QJsonObject PlayerProfile::toJson() const
{
	return {{QStringLiteral("username"), username},
			{QStringLiteral("displayName"), displayName},
			{QStringLiteral("avatarUrl"), avatarUrl},
			{QStringLiteral("countryCode"), countryCode},
			{QStringLiteral("totalScore"), totalScore},
			{QStringLiteral("guessingScore"), guessingScore},
			{QStringLiteral("drawingScore"), drawingScore},
			{QStringLiteral("roundsWon"), roundsWon},
			{QStringLiteral("roundsLost"), roundsLost},
			{QStringLiteral("roundsAbandoned"), roundsAbandoned},
			{QStringLiteral("leaderboardRank"), leaderboardRank},
			{QStringLiteral("guessingLeaderboardRank"), guessingLeaderboardRank},
			{QStringLiteral("drawingLeaderboardRank"), drawingLeaderboardRank},
			{QStringLiteral("guessingRank"), guessingRank},
			{QStringLiteral("drawingRank"), drawingRank},
			{QStringLiteral("serverRole"), serverRole},
			{QStringLiteral("masterSuperAdmin"), masterSuperAdmin}};
}

PlayerProfile PlayerProfile::fromJson(const QJsonObject& object)
{
	PlayerProfile profile;
	profile.username = object.value(QStringLiteral("username")).toString();
	profile.displayName = object.value(QStringLiteral("displayName")).toString(profile.username);
	profile.avatarUrl = object.value(QStringLiteral("avatarUrl")).toString();
	profile.countryCode = object.value(QStringLiteral("countryCode")).toString().toUpper();
	profile.totalScore = qMax(0, object.value(QStringLiteral("totalScore")).toInt());
	profile.guessingScore = qMax(0, object.value(QStringLiteral("guessingScore")).toInt(profile.totalScore));
	profile.drawingScore = qMax(0, object.value(QStringLiteral("drawingScore")).toInt());
	profile.roundsWon = qMax(0, object.value(QStringLiteral("roundsWon")).toInt());
	profile.roundsLost = qMax(0, object.value(QStringLiteral("roundsLost")).toInt());
	profile.roundsAbandoned = qMax(0, object.value(QStringLiteral("roundsAbandoned")).toInt());
	profile.leaderboardRank = qMax(0, object.value(QStringLiteral("leaderboardRank")).toInt());
	profile.guessingLeaderboardRank = qMax(0, object.value(QStringLiteral("guessingLeaderboardRank")).toInt());
	profile.drawingLeaderboardRank = qMax(0, object.value(QStringLiteral("drawingLeaderboardRank")).toInt());
	profile.guessingRank = object.value(QStringLiteral("guessingRank")).toString();
	profile.drawingRank = object.value(QStringLiteral("drawingRank")).toString();
	profile.serverRole = object.value(QStringLiteral("serverRole")).toString(QStringLiteral("player"));
	profile.masterSuperAdmin = object.value(QStringLiteral("masterSuperAdmin")).toBool();
	return profile;
}

QJsonObject UserDirectoryEntry::toJson() const
{
	return {{QStringLiteral("profile"), profile.toJson()},
		{QStringLiteral("presenceStatus"), presenceStatus},
		{QStringLiteral("relationship"), relationship},
		{QStringLiteral("relationshipId"), static_cast<double>(relationshipId)},
		{QStringLiteral("hasRoom"), hasRoom},
		{QStringLiteral("room"), hasRoom ? room.toJson() : QJsonObject{}},
		{QStringLiteral("activeBans"), activeBans}};
}

UserDirectoryEntry UserDirectoryEntry::fromJson(const QJsonObject& object)
{
	UserDirectoryEntry entry;
	entry.profile = PlayerProfile::fromJson(object.value(QStringLiteral("profile")).toObject());
	entry.presenceStatus = object.value(QStringLiteral("presenceStatus")).toString(QStringLiteral("offline"));
	entry.relationship = object.value(QStringLiteral("relationship")).toString(QStringLiteral("none"));
	entry.relationshipId = static_cast<qint64>(object.value(QStringLiteral("relationshipId")).toDouble());
	entry.hasRoom = object.value(QStringLiteral("hasRoom")).toBool();
	entry.room = RoomInfo::fromJson(object.value(QStringLiteral("room")).toObject());
	entry.activeBans = object.value(QStringLiteral("activeBans")).toArray();
	return entry;
}

QJsonObject FriendRequest::toJson() const
{
	return {{QStringLiteral("id"), static_cast<double>(id)},
		{QStringLiteral("senderUsername"), senderUsername},
		{QStringLiteral("recipientUsername"), recipientUsername},
		{QStringLiteral("status"), status},
		{QStringLiteral("createdAt"), static_cast<double>(createdAt)}};
}

FriendRequest FriendRequest::fromJson(const QJsonObject& object)
{
	FriendRequest request;
	request.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
	request.senderUsername = object.value(QStringLiteral("senderUsername")).toString();
	request.recipientUsername = object.value(QStringLiteral("recipientUsername")).toString();
	request.status = object.value(QStringLiteral("status")).toString();
	request.createdAt = static_cast<qint64>(object.value(QStringLiteral("createdAt")).toDouble());
	return request;
}

QJsonObject DirectMessage::toJson() const
{
	return {{QStringLiteral("id"), static_cast<double>(id)},
		{QStringLiteral("senderUsername"), senderUsername},
		{QStringLiteral("recipientUsername"), recipientUsername},
		{QStringLiteral("body"), body},
		{QStringLiteral("createdAt"), static_cast<double>(createdAt)}};
}

DirectMessage DirectMessage::fromJson(const QJsonObject& object)
{
	DirectMessage message;
	message.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
	message.senderUsername = object.value(QStringLiteral("senderUsername")).toString();
	message.recipientUsername = object.value(QStringLiteral("recipientUsername")).toString();
	message.body = object.value(QStringLiteral("body")).toString();
	message.createdAt = static_cast<qint64>(object.value(QStringLiteral("createdAt")).toDouble());
	return message;
}

QJsonObject ServerRoleAssignment::toJson() const
{
	return {{QStringLiteral("serverId"), serverId}, {QStringLiteral("username"), username},
		{QStringLiteral("role"), role}, {QStringLiteral("assignedBy"), assignedBy}};
}

ServerRoleAssignment ServerRoleAssignment::fromJson(const QJsonObject& object)
{
	ServerRoleAssignment assignment;
	assignment.serverId = object.value(QStringLiteral("serverId")).toString();
	assignment.username = object.value(QStringLiteral("username")).toString().toLower();
	assignment.role = object.value(QStringLiteral("role")).toString(QStringLiteral("player"));
	assignment.assignedBy = object.value(QStringLiteral("assignedBy")).toString().toLower();
	return assignment;
}

QJsonObject ServerSanction::toJson() const
{
	return {{QStringLiteral("id"), id}, {QStringLiteral("serverId"), serverId},
		{QStringLiteral("targetUsername"), targetUsername}, {QStringLiteral("kind"), kind},
		{QStringLiteral("textChat"), textChat}, {QStringLiteral("voiceChat"), voiceChat},
		{QStringLiteral("reason"), reason}, {QStringLiteral("issuedBy"), issuedBy},
		{QStringLiteral("createdAt"), createdAt}, {QStringLiteral("expiresAt"), expiresAt}};
}

ServerSanction ServerSanction::fromJson(const QJsonObject& object)
{
	ServerSanction sanction;
	sanction.id = qMax(qint64(0), static_cast<qint64>(object.value(QStringLiteral("id")).toDouble()));
	sanction.serverId = object.value(QStringLiteral("serverId")).toString();
	sanction.targetUsername = object.value(QStringLiteral("targetUsername")).toString().toLower();
	sanction.kind = object.value(QStringLiteral("kind")).toString();
	sanction.textChat = object.value(QStringLiteral("textChat")).toBool();
	sanction.voiceChat = object.value(QStringLiteral("voiceChat")).toBool();
	sanction.reason = object.value(QStringLiteral("reason")).toString();
	sanction.issuedBy = object.value(QStringLiteral("issuedBy")).toString().toLower();
	sanction.createdAt = static_cast<qint64>(object.value(QStringLiteral("createdAt")).toDouble());
	sanction.expiresAt = static_cast<qint64>(object.value(QStringLiteral("expiresAt")).toDouble());
	return sanction;
}

QJsonObject PlayerMutePreference::toJson() const
{
	return {{QStringLiteral("serverId"), serverId}, {QStringLiteral("muterUsername"), muterUsername},
		{QStringLiteral("targetUsername"), targetUsername}, {QStringLiteral("textChat"), textChat},
		{QStringLiteral("voiceChat"), voiceChat}};
}

PlayerMutePreference PlayerMutePreference::fromJson(const QJsonObject& object)
{
	PlayerMutePreference preference;
	preference.serverId = object.value(QStringLiteral("serverId")).toString();
	preference.muterUsername = object.value(QStringLiteral("muterUsername")).toString().toLower();
	preference.targetUsername = object.value(QStringLiteral("targetUsername")).toString().toLower();
	preference.textChat = object.value(QStringLiteral("textChat")).toBool();
	preference.voiceChat = object.value(QStringLiteral("voiceChat")).toBool();
	return preference;
}

QJsonObject DrawingStroke::toJson() const
{
	return {{QStringLiteral("color"), color},
			{QStringLiteral("width"), qBound(1, width, 32)},
			{QStringLiteral("points"), points}};
}

DrawingStroke DrawingStroke::fromJson(const QJsonObject& object)
{
	DrawingStroke stroke;
	stroke.color = object.value(QStringLiteral("color")).toString(QStringLiteral("#202124"));
	stroke.width = qBound(1, object.value(QStringLiteral("width")).toInt(4), 32);
	stroke.points = object.value(QStringLiteral("points")).toArray();
	return stroke;
}

QJsonObject CanvasLayer::toJson() const
{
	return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
			{QStringLiteral("order"), order}, {QStringLiteral("visible"), visible},
			{QStringLiteral("locked"), locked}};
}

CanvasLayer CanvasLayer::fromJson(const QJsonObject& object)
{
	CanvasLayer layer;
	layer.id = object.value(QStringLiteral("id")).toString();
	layer.name = object.value(QStringLiteral("name")).toString();
	layer.order = object.value(QStringLiteral("order")).toInt();
	layer.visible = object.value(QStringLiteral("visible")).toBool(true);
	layer.locked = object.value(QStringLiteral("locked")).toBool(false);
	return layer;
}

QJsonObject CanvasObject::toJson() const
{
	return {{QStringLiteral("id"), id}, {QStringLiteral("layerId"), layerId},
			{QStringLiteral("type"), type}, {QStringLiteral("color"), color},
			{QStringLiteral("fillColor"), fillColor}, {QStringLiteral("text"), text},
			{QStringLiteral("imageData"), imageData}, {QStringLiteral("points"), points}, {QStringLiteral("x"), x},
			{QStringLiteral("y"), y}, {QStringLiteral("width"), width},
			{QStringLiteral("height"), height}, {QStringLiteral("strokeWidth"), strokeWidth},
			{QStringLiteral("fontSize"), fontSize}, {QStringLiteral("filled"), filled}};
}

CanvasObject CanvasObject::fromJson(const QJsonObject& object)
{
	CanvasObject item;
	item.id = object.value(QStringLiteral("id")).toString();
	item.layerId = object.value(QStringLiteral("layerId")).toString();
	item.type = object.value(QStringLiteral("type")).toString();
	item.color = object.value(QStringLiteral("color")).toString(QStringLiteral("#202124"));
	item.fillColor = object.value(QStringLiteral("fillColor")).toString(QStringLiteral("#ffffff"));
	item.text = object.value(QStringLiteral("text")).toString();
	item.imageData = object.value(QStringLiteral("imageData")).toString();
	item.points = object.value(QStringLiteral("points")).toArray();
	item.x = object.value(QStringLiteral("x")).toDouble();
	item.y = object.value(QStringLiteral("y")).toDouble();
	item.width = object.value(QStringLiteral("width")).toDouble();
	item.height = object.value(QStringLiteral("height")).toDouble();
	item.strokeWidth = object.value(QStringLiteral("strokeWidth")).toInt(4);
	item.fontSize = object.value(QStringLiteral("fontSize")).toInt(24);
	item.filled = object.value(QStringLiteral("filled")).toBool(false);
	return item;
}

bool isValidSingleLineText(const QString& text, qsizetype maximumLength)
{
	if (maximumLength < 1 || text.size() > maximumLength || text.trimmed().isEmpty())
		return false;
	for (const QChar character : text) {
		if (character.category() == QChar::Other_Control)
			return false;
	}
	return true;
}

bool isValidCanvasLayer(const CanvasLayer& layer, QString* error)
{
	if (!validId(layer.id)) {
		setValidationError(error, QStringLiteral("Invalid layer ID"));
		return false;
	}
	if (!isValidSingleLineText(layer.name, 32)) {
		setValidationError(error, QStringLiteral("Layer names must contain 1-32 characters without control characters"));
		return false;
	}
	if (layer.order < 0 || layer.order >= MaximumCanvasLayers) {
		setValidationError(error, QStringLiteral("Invalid layer order"));
		return false;
	}
	return true;
}

bool isValidCanvasObject(const CanvasObject& item, QString* error)
{
	if (!validId(item.id) || !validId(item.layerId)) {
		setValidationError(error, QStringLiteral("Invalid object or layer ID"));
		return false;
	}
	if (item.type != QStringLiteral("freehand") && item.type != QStringLiteral("brush")
		&& item.type != QStringLiteral("airbrush") && item.type != QStringLiteral("curve")
		&& item.type != QStringLiteral("polygon") && item.type != QStringLiteral("rectangle")
		&& item.type != QStringLiteral("rounded_rectangle") && item.type != QStringLiteral("ellipse")
		&& item.type != QStringLiteral("line") && item.type != QStringLiteral("fill")
		&& item.type != QStringLiteral("text")) {
		setValidationError(error, QStringLiteral("Unsupported canvas object type"));
		return false;
	}
	if (!validColor(item.color) || !validColor(item.fillColor)) {
		setValidationError(error, QStringLiteral("Invalid object color"));
		return false;
	}
	if (!std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.width)
		|| !std::isfinite(item.height) || item.x < 0.0 || item.y < 0.0 || item.width < 0.0
		|| item.height < 0.0 || item.x + item.width > 1.000001 || item.y + item.height > 1.000001) {
		setValidationError(error, QStringLiteral("Object geometry is outside the canvas"));
		return false;
	}
	if (item.strokeWidth < 1 || item.strokeWidth > 32 || item.fontSize < 8 || item.fontSize > 96) {
		setValidationError(error, QStringLiteral("Invalid stroke width or text size"));
		return false;
	}
	if (item.type == QStringLiteral("text")) {
		if (item.text.trimmed().isEmpty() || item.text.size() > 256) {
			setValidationError(error, QStringLiteral("Text objects must contain 1-256 characters"));
			return false;
		}
	} else if (!item.text.isEmpty()) {
		setValidationError(error, QStringLiteral("Only text objects may contain text"));
		return false;
	}
	if (item.type == QStringLiteral("fill")) {
		if (!validCanvasImageData(item.imageData) || !item.points.isEmpty()) {
			setValidationError(error, QStringLiteral("Fill objects require a valid bounded PNG image"));
			return false;
		}
	} else if (!item.imageData.isEmpty()) {
		setValidationError(error, QStringLiteral("Only fill objects may contain image data"));
		return false;
	}
	if (item.type == QStringLiteral("freehand") || item.type == QStringLiteral("brush")
		|| item.type == QStringLiteral("airbrush") || item.type == QStringLiteral("curve")) {
		if (item.points.isEmpty() || !validPoints(item.points)) {
			setValidationError(error, QStringLiteral("Stroke points are invalid or exceed the limit"));
			return false;
		}
	} else if (item.type == QStringLiteral("polygon")) {
		if (item.points.size() < 3 || !validPoints(item.points)) {
			setValidationError(error, QStringLiteral("Polygons require at least three normalized points"));
			return false;
		}
	} else if (item.type == QStringLiteral("line")) {
		if (item.points.size() != 2 || !validPoints(item.points)) {
			setValidationError(error, QStringLiteral("Lines require two normalized endpoints"));
			return false;
		}
	} else if (item.points.size() != 0) {
		setValidationError(error, QStringLiteral("This object type cannot contain path points"));
		return false;
	}
	return true;
}

}
