#pragma once

#include "shared/models.h"

#include <QSqlDatabase>
#include <QString>
#include <optional>

namespace gtd {

class AccountStore {
public:
	explicit AccountStore(const QString& databasePath);
	~AccountStore();

	AccountStore(const AccountStore&) = delete;
	AccountStore& operator=(const AccountStore&) = delete;

	bool initialize(QString* error = nullptr);
	bool createAccount(const QString& username, const QString& password,
					   const QString& displayName, QString* error = nullptr);
	std::optional<PlayerProfile> authenticate(const QString& username, const QString& password,
											   QString* error = nullptr);
	std::optional<PlayerProfile> profile(const QString& username, QString* error = nullptr);
	bool updateCountry(const QString& username, const QString& countryCode, QString* error = nullptr);
	bool updateProfile(const QString& username, const QString& displayName, const QString& countryCode,
		const QString& currentPassword, const QString& newPassword, QString* error = nullptr);
	QList<PlayerProfile> topPlayers(bool drawingPoints, int limit = 1000, QString* error = nullptr);
	bool addScore(const QString& username, int points, bool drawingPoints, QString* error = nullptr);
	bool recordRoundResult(const QString& username, bool roundWon, QString* error = nullptr);

private:
	std::optional<PlayerProfile> loadProfile(const QString& normalizedUsername, QString* error);
	void addProfileRank(PlayerProfile* profile);
	QString databasePath_;
	QString connectionName_;
	QSqlDatabase database_;
};

}
