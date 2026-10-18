#include "shared/protocol.h"

#include <QJsonDocument>
#include <QTcpSocket>

namespace gtd {

QByteArray encodeMessage(const QJsonObject& message)
{
	QJsonObject framed = message;
	framed.insert(QStringLiteral("version"), ProtocolVersion);
	return QJsonDocument(framed).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject makeMessage(const QString& type, const QJsonObject& fields)
{
	QJsonObject message = fields;
	message.insert(QStringLiteral("version"), ProtocolVersion);
	message.insert(QStringLiteral("type"), type);
	return message;
}

bool parseMessage(const QByteArray& frame, QJsonObject* message, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error)
			*error = QStringLiteral("Invalid JSON message");
		return false;
	}

	const QJsonObject object = document.object();
	if (object.value(QStringLiteral("version")).toInt(-1) != ProtocolVersion
		|| !object.value(QStringLiteral("type")).isString()
		|| object.value(QStringLiteral("type")).toString().isEmpty()) {
		if (error)
			*error = QStringLiteral("Unsupported protocol version or missing message type");
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

	*pending += incoming;
	if (pending->size() > MaximumFrameBytes && !pending->contains('\n')) {
		if (error)
			*error = QStringLiteral("Message exceeds the maximum frame size");
		pending->clear();
		return messages;
	}

	while (true) {
		const qsizetype newline = pending->indexOf('\n');
		if (newline < 0)
			break;
		if (newline > MaximumFrameBytes) {
			if (error)
				*error = QStringLiteral("Message exceeds the maximum frame size");
			pending->clear();
			return {};
		}
		const QByteArray frame = pending->left(newline).trimmed();
		pending->remove(0, newline + 1);
		if (frame.isEmpty())
			continue;

		QJsonObject message;
		if (!parseMessage(frame, &message, error)) {
			pending->clear();
			return {};
		}
		messages.append(message);
	}

	if (pending->size() > MaximumFrameBytes) {
		if (error)
			*error = QStringLiteral("Message exceeds the maximum frame size");
		pending->clear();
		return {};
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
