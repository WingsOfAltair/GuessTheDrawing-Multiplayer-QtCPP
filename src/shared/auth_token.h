#pragma once

#include <QString>

namespace gtd {

QString createSessionToken(const QString& username, const QString& displayName,
						   const QString& secret, qint64 expiresAt);
bool verifySessionToken(const QString& token, const QString& secret, QString* username,
						qint64 now = -1, QString* displayName = nullptr);

}
