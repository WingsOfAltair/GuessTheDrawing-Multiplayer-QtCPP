#include "shared/auth_token.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace gtd {
namespace {

QByteArray encodeBase64Url(const QByteArray& value)
{
	return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
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

}

QString createSessionToken(const QString& username, const QString& displayName,
						   const QString& secret, qint64 expiresAt)
{
	QJsonObject payload{{QStringLiteral("username"), username},
						{QStringLiteral("displayName"), displayName},
						{QStringLiteral("expiresAt"), expiresAt},
						{QStringLiteral("nonce"), QString::number(QRandomGenerator::global()->generate64(), 16)}};
	const QByteArray encodedPayload = encodeBase64Url(QJsonDocument(payload).toJson(QJsonDocument::Compact));
	const QByteArray signature = QMessageAuthenticationCode::hash(
		encodedPayload, secret.toUtf8(), QCryptographicHash::Sha256);
	return QString::fromLatin1(encodedPayload + '.' + encodeBase64Url(signature));
}

bool verifySessionToken(const QString& token, const QString& secret, QString* username,
						qint64 now, QString* displayName)
{
	if (secret.isEmpty())
		return false;
	const QByteArray encoded = token.toLatin1();
	const qsizetype separator = encoded.indexOf('.');
	if (separator <= 0 || separator == encoded.size() - 1)
		return false;

	const QByteArray encodedPayload = encoded.left(separator);
	const QByteArray suppliedSignature = QByteArray::fromBase64(
		encoded.mid(separator + 1), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
	const QByteArray expectedSignature = QMessageAuthenticationCode::hash(
		encodedPayload, secret.toUtf8(), QCryptographicHash::Sha256);
	if (!constantTimeEqual(suppliedSignature, expectedSignature))
		return false;

	const QByteArray payloadBytes = QByteArray::fromBase64(
		encodedPayload, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
	QJsonParseError parseError;
	const QJsonDocument payloadDocument = QJsonDocument::fromJson(payloadBytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !payloadDocument.isObject())
		return false;

	const QJsonObject payload = payloadDocument.object();
	const QString tokenUsername = payload.value(QStringLiteral("username")).toString();
	const qint64 expiration = static_cast<qint64>(payload.value(QStringLiteral("expiresAt")).toDouble());
	if (tokenUsername.isEmpty())
		return false;
	if (now < 0)
		now = QDateTime::currentSecsSinceEpoch();
	if (expiration <= now)
		return false;
	if (username)
		*username = tokenUsername;
	if (displayName)
		*displayName = payload.value(QStringLiteral("displayName")).toString(tokenUsername);
	return true;
}

}
