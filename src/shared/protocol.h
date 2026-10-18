#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class QTcpSocket;

namespace gtd {

inline constexpr int ProtocolVersion = 1;
inline constexpr qsizetype MaximumFrameBytes = 1024 * 1024;

QByteArray encodeMessage(const QJsonObject& message);
QJsonObject makeMessage(const QString& type, const QJsonObject& fields = {});
bool parseMessage(const QByteArray& frame, QJsonObject* message, QString* error = nullptr);
QList<QJsonObject> takeMessages(QByteArray* pending, const QByteArray& incoming, QString* error = nullptr);
bool sendMessage(QTcpSocket* socket, const QJsonObject& message);

}
