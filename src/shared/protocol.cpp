#include "shared/protocol.h"

#include "shared/app_version.h"

#include <QJsonDocument>
#include <QTcpSocket>

namespace gtd {

QByteArray encodeMessage(const QJsonObject& message)
{
	QJsonObject framed = message;
	framed.insert(QStringLiteral("version"), ProtocolVersion);
	framed.insert(QStringLiteral("appVersion"), ApplicationVersion);
	return QJsonDocument(framed).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject makeMessage(const QString& type, const QJsonObject& fields)
{
	QJsonObject message = fields;
	message.insert(QStringLiteral("version"), ProtocolVersion);
	message.insert(QStringLiteral("appVersion"), ApplicationVersion);
	message.insert(QStringLiteral("type"), type);
	return message;
}

bool parseMessage(const QByteArray& frame, QJsonObject* message, QString* error)
{
	if (error)
		error->clear();
	if (frame.size() > MaximumFrameBytes) {
		if (error)
			*error = QStringLiteral("Message exceeds the maximum frame size");
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error)
			*error = QStringLiteral("Invalid JSON message");
		return false;
	}

	const QJsonObject object = document.object();
	const QString type = object.value(QStringLiteral("type")).toString();
	if (object.value(QStringLiteral("version")).toInt(-1) != ProtocolVersion
		|| !object.value(QStringLiteral("type")).isString()
		|| type.isEmpty() || type.size() > MaximumMessageTypeLength) {
		if (error)
			*error = QStringLiteral("Unsupported protocol version or invalid message type");
		return false;
	}
	auto validStringField = [&object](const QString& key, qsizetype maximumLength) {
		const QJsonValue value = object.value(key);
		return value.isUndefined() || (value.isString() && value.toString().size() <= maximumLength);
	};
	if (!validStringField(QStringLiteral("requestId"), 128)
		|| !validStringField(QStringLiteral("appVersion"), 32)
		|| !validStringField(QStringLiteral("username"), 24)
		|| !validStringField(QStringLiteral("password"), 128)
		|| !validStringField(QStringLiteral("currentPassword"), 128)
		|| !validStringField(QStringLiteral("newPassword"), 128)
		|| !validStringField(QStringLiteral("displayName"), 32)
		|| !validStringField(QStringLiteral("countryCode"), 6)
		|| !validStringField(QStringLiteral("authToken"), 512)
		|| !validStringField(QStringLiteral("passkeySalt"), 128)
		|| !validStringField(QStringLiteral("passkeyChallenge"), 128)
		|| !validStringField(QStringLiteral("passkeyProof"), 128)
		|| !validStringField(QStringLiteral("text"), 4096)
		|| !validStringField(QStringLiteral("serverId"), 48)
		|| !validStringField(QStringLiteral("roomId"), 64)
		|| !validStringField(QStringLiteral("host"), 253)) {
		if (error)
			*error = QStringLiteral("Invalid or oversized message field");
		return false;
	}

	if (message)
		*message = object;
	return true;
}

QList<QJsonObject> takeMessages(QByteArray* pending, const QByteArray& incoming, QString* error)
{
	QList<QJsonObject> messages;
	if (!pending)
		return messages;
	if (error)
		error->clear();
	if (pending->size() > MaximumFrameBytes) {
		if (error)
			*error = QStringLiteral("Message exceeds the maximum frame size");
		pending->clear();
		return {};
	}

	qsizetype offset = 0;
	while (offset < incoming.size()) {
		const qsizetype newline = incoming.indexOf('\n', offset);
		const qsizetype segmentEnd = newline < 0 ? incoming.size() : newline;
		const qsizetype segmentSize = segmentEnd - offset;
		if (pending->size() + segmentSize > MaximumFrameBytes) {
			if (error)
				*error = QStringLiteral("Message exceeds the maximum frame size");
			pending->clear();
			return {};
		}
		if (segmentSize > 0)
			pending->append(incoming.constData() + offset, segmentSize);
		if (newline < 0)
			break;

		const QByteArray frame = pending->trimmed();
		pending->clear();
		offset = newline + 1;
		if (frame.isEmpty())
			continue;
		if (messages.size() >= MaximumMessagesPerRead) {
			if (error)
				*error = QStringLiteral("Too many messages in one read");
			return {};
		}

		QJsonObject message;
		if (!parseMessage(frame, &message, error))
			return {};
		messages.append(message);
	}
	return messages;
}

bool sendMessage(QTcpSocket* socket, const QJsonObject& message)
{
	if (!socket || socket->state() != QAbstractSocket::ConnectedState)
		return false;
	const QByteArray frame = encodeMessage(message);
	return socket->write(frame) == frame.size();
}

}
