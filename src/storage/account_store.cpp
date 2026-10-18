#include "storage/account_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <iterator>

namespace gtd {
namespace {

constexpr int PasswordIterations = 210000;
constexpr qsizetype PasswordHashBytes = 32;

QString normalizedUsername(const QString& username)
{
	return username.trimmed().toLower();
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
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

QByteArray randomSalt()
{
	QByteArray salt(16, Qt::Uninitialized);
	for (qsizetype index = 0; index < salt.size(); index += 4) {
		const quint32 value = QRandomGenerator::system()->generate();
		const qsizetype remaining = qMin(qsizetype(4), salt.size() - index);
		for (qsizetype offset = 0; offset < remaining; ++offset)
			salt[index + offset] = static_cast<char>((value >> (offset * 8)) & 0xff);
	}
	return salt;
}

PlayerProfile profileFromQuery(const QSqlQuery& query)
{
	PlayerProfile profile;
	profile.username = query.value(0).toString();
	profile.displayName = query.value(1).toString();
	profile.countryCode = query.value(2).toString();
	profile.avatarUrl = query.value(3).toString();
	profile.guessingScore = query.value(4).toInt();
	profile.drawingScore = query.value(5).toInt();
	profile.totalScore = profile.guessingScore + profile.drawingScore;
	profile.roundsWon = query.value(6).toInt();
	profile.roundsLost = query.value(7).toInt();
	return profile;
}

}

AccountStore::AccountStore(const QString& databasePath)
	: databasePath_(databasePath),
	  connectionName_(QStringLiteral("gtd-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

AccountStore::~AccountStore()
{
	database_.close();
	database_ = QSqlDatabase();
	QSqlDatabase::removeDatabase(connectionName_);
}

bool AccountStore::initialize(QString* error)
{
	if (databasePath_ != QStringLiteral(":memory:")) {
		const QFileInfo info(databasePath_);
		if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
			setError(error, QStringLiteral("Unable to create database directory"));
			return false;
		}
	}

	database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
	database_.setDatabaseName(databasePath_);
	if (!database_.open()) {
		setError(error, database_.lastError().text());
		return false;
	}

	QSqlQuery query(database_);
	if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral(
			"CREATE TABLE IF NOT EXISTS accounts ("
			"username TEXT PRIMARY KEY NOT NULL,"
			"display_name TEXT NOT NULL,"
			"avatar_url TEXT NOT NULL DEFAULT '',"
			"password_salt BLOB NOT NULL,"
			"password_hash BLOB NOT NULL,"
			"total_score INTEGER NOT NULL DEFAULT 0 CHECK(total_score >= 0),"
			"rounds_won INTEGER NOT NULL DEFAULT 0 CHECK(rounds_won >= 0),"
			"created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN rounds_lost INTEGER NOT NULL DEFAULT 0 CHECK(rounds_lost >= 0)"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN drawing_score INTEGER NOT NULL DEFAULT 0 CHECK(drawing_score >= 0)"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN guessing_score INTEGER NOT NULL DEFAULT 0 CHECK(guessing_score >= 0)"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral(
			"UPDATE accounts SET guessing_score = total_score "
			"WHERE guessing_score = 0 AND total_score > 0"))) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN score_order INTEGER NOT NULL DEFAULT 0"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN guessing_order INTEGER NOT NULL DEFAULT 0"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN drawing_order INTEGER NOT NULL DEFAULT 0"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN country_code TEXT NOT NULL DEFAULT ''"))
		&& !query.lastError().text().contains(QStringLiteral("duplicate column name"), Qt::CaseInsensitive)) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral(
			"UPDATE accounts SET score_order = rowid "
			"WHERE score_order = 0 AND guessing_score + drawing_score > 0"))) {
		setError(error, query.lastError().text());
		return false;
	}
	if (!query.exec(QStringLiteral(
			"UPDATE accounts SET guessing_order = rowid WHERE guessing_order = 0 AND guessing_score > 0"))
		|| !query.exec(QStringLiteral(
			"UPDATE accounts SET drawing_order = rowid WHERE drawing_order = 0 AND drawing_score > 0"))) {
		setError(error, query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::createAccount(const QString& username, const QString& password,
								 const QString& displayName, QString* error)
{
	const QString normalized = normalizedUsername(username);
	static const QRegularExpression usernamePattern(QStringLiteral("^[a-zA-Z0-9_]{3,24}$"));
	if (!usernamePattern.match(username.trimmed()).hasMatch()) {
		setError(error, QStringLiteral("Username must be 3-24 letters, numbers, or underscores"));
		return false;
	}
	if (password.size() < 8 || password.size() > 128) {
		setError(error, QStringLiteral("Password must be 8-128 characters"));
		return false;
	}
	const QString safeDisplayName = displayName.trimmed().left(32);
	if (safeDisplayName.isEmpty()) {
		setError(error, QStringLiteral("Display name cannot be empty"));
		return false;
	}

	const QByteArray salt = randomSalt();
	const QByteArray hash = QPasswordDigestor::deriveKeyPbkdf2(
		QCryptographicHash::Sha256, password.toUtf8(), salt, PasswordIterations, PasswordHashBytes);
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"INSERT INTO accounts(username, display_name, password_salt, password_hash) "
		"VALUES(:username, :display_name, :salt, :hash)"));
	query.bindValue(QStringLiteral(":username"), normalized);
	query.bindValue(QStringLiteral(":display_name"), safeDisplayName);
	query.bindValue(QStringLiteral(":salt"), salt);
	query.bindValue(QStringLiteral(":hash"), hash);
	if (!query.exec()) {
		setError(error, query.lastError().text().contains(QStringLiteral("UNIQUE"), Qt::CaseInsensitive)
							? QStringLiteral("That username is already in use")
							: query.lastError().text());
		return false;
	}
	return true;
}

std::optional<PlayerProfile> AccountStore::authenticate(const QString& username, const QString& password,
														QString* error)
{
	const QString normalized = normalizedUsername(username);
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("SELECT password_salt, password_hash FROM accounts WHERE username = :username"));
	query.bindValue(QStringLiteral(":username"), normalized);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return std::nullopt;
	}
	if (!query.next()) {
		setError(error, QStringLiteral("Invalid username or password"));
		return std::nullopt;
	}

	const QByteArray salt = query.value(0).toByteArray();
	const QByteArray expected = query.value(1).toByteArray();
	const QByteArray actual = QPasswordDigestor::deriveKeyPbkdf2(
		QCryptographicHash::Sha256, password.toUtf8(), salt, PasswordIterations, PasswordHashBytes);
	if (!constantTimeEqual(actual, expected)) {
		setError(error, QStringLiteral("Invalid username or password"));
		return std::nullopt;
	}
	return loadProfile(normalized, error);
}

std::optional<PlayerProfile> AccountStore::profile(const QString& username, QString* error)
{
	return loadProfile(normalizedUsername(username), error);
}

bool AccountStore::updateCountry(const QString& username, const QString& countryCode, QString* error)
{
	const QString normalizedCountryCode = countryCode.trimmed().toUpper();
	static const QStringList validCountryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FO FR GA GB GD GE GF GG GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT HU "
		"ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS LT "
		"LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI NL "
		"NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG SH "
		"SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG UM "
		"US UY UZ VA VC VE VG VI VN VU WF WS YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (!normalizedCountryCode.isEmpty() && !validCountryCodes.contains(normalizedCountryCode)) {
		setError(error, QStringLiteral("Choose a valid ISO country code"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("UPDATE accounts SET country_code = :country WHERE username = :username"));
	query.bindValue(QStringLiteral(":country"), normalizedCountryCode);
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Profile not found") : query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::updateProfile(const QString& username, const QString& displayName, const QString& countryCode,
	const QString& currentPassword, const QString& newPassword, QString* error)
{
	const QString safeDisplayName = displayName.trimmed().left(32);
	if (safeDisplayName.isEmpty()) {
		setError(error, QStringLiteral("Nickname cannot be empty"));
		return false;
	}
	const QString normalizedCountryCode = countryCode.trimmed().toUpper();
	static const QStringList validCountryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FO FR GA GB GD GE GF GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT HU "
		"ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS LT "
		"LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI NL "
		"NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG SH "
		"SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG UM "
		"US UY UZ VA VC VE VG VI VN VU WF WS YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (!normalizedCountryCode.isEmpty() && !validCountryCodes.contains(normalizedCountryCode)) {
		setError(error, QStringLiteral("Choose a valid ISO country code"));
		return false;
	}
	if (!newPassword.isEmpty() && (newPassword.size() < 8 || newPassword.size() > 128)) {
		setError(error, QStringLiteral("New password must be 8-128 characters"));
		return false;
	}
	if (newPassword.isEmpty() != currentPassword.isEmpty()) {
		setError(error, QStringLiteral("Enter the current password and a new password to change it"));
		return false;
	}

	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QByteArray newSalt;
	QByteArray newHash;
	if (!newPassword.isEmpty()) {
		QSqlQuery credentials(database_);
		credentials.prepare(QStringLiteral("SELECT password_salt, password_hash FROM accounts WHERE username = :username"));
		credentials.bindValue(QStringLiteral(":username"), normalizedUsername(username));
		if (!credentials.exec() || !credentials.next()) {
			database_.rollback();
			setError(error, QStringLiteral("Profile not found"));
			return false;
		}
		const QByteArray expected = credentials.value(1).toByteArray();
		const QByteArray actual = QPasswordDigestor::deriveKeyPbkdf2(
			QCryptographicHash::Sha256, currentPassword.toUtf8(), credentials.value(0).toByteArray(),
			PasswordIterations, PasswordHashBytes);
		if (!constantTimeEqual(actual, expected)) {
			database_.rollback();
			setError(error, QStringLiteral("Current password is incorrect"));
			return false;
		}
		newSalt = randomSalt();
		newHash = QPasswordDigestor::deriveKeyPbkdf2(
			QCryptographicHash::Sha256, newPassword.toUtf8(), newSalt, PasswordIterations, PasswordHashBytes);
	}

	QSqlQuery update(database_);
	update.prepare(newPassword.isEmpty()
		? QStringLiteral("UPDATE accounts SET display_name = :displayName, country_code = :country WHERE username = :username")
		: QStringLiteral("UPDATE accounts SET display_name = :displayName, country_code = :country, "
			"password_salt = :salt, password_hash = :hash WHERE username = :username"));
	update.bindValue(QStringLiteral(":displayName"), safeDisplayName);
	update.bindValue(QStringLiteral(":country"), normalizedCountryCode);
	update.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!newPassword.isEmpty()) {
		update.bindValue(QStringLiteral(":salt"), newSalt);
		update.bindValue(QStringLiteral(":hash"), newHash);
	}
	if (!update.exec() || update.numRowsAffected() != 1) {
		database_.rollback();
		setError(error, update.lastError().text().isEmpty()
			? QStringLiteral("Profile not found") : update.lastError().text());
		return false;
	}
	if (!database_.commit()) {
		database_.rollback();
		setError(error, database_.lastError().text());
		return false;
	}
	return true;
}

QList<PlayerProfile> AccountStore::topPlayers(bool drawingPoints, int limit, QString* error)
{
	const int boundedLimit = qBound(1, limit, 1000);
	const QString scoreColumn = drawingPoints ? QStringLiteral("drawing_score") : QStringLiteral("guessing_score");
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT username, display_name, country_code, avatar_url, guessing_score, drawing_score, rounds_won, rounds_lost "
		"FROM accounts WHERE %1 > 0 ORDER BY %1 DESC, %2 ASC, username ASC LIMIT :limit")
		.arg(scoreColumn, drawingPoints ? QStringLiteral("drawing_order") : QStringLiteral("guessing_order")));
	query.bindValue(QStringLiteral(":limit"), boundedLimit);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<PlayerProfile> profiles;
	while (query.next()) {
		PlayerProfile profile = profileFromQuery(query);
		if (drawingPoints)
			profile.drawingLeaderboardRank = profiles.size() + 1;
		else
			profile.guessingLeaderboardRank = profiles.size() + 1;
		profiles.append(profile);
	}
	return profiles;
}

std::optional<PlayerProfile> AccountStore::loadProfile(const QString& normalizedUsername, QString* error)
{
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT username, display_name, country_code, avatar_url, guessing_score, drawing_score, rounds_won, rounds_lost "
		"FROM accounts WHERE username = :username"));
	query.bindValue(QStringLiteral(":username"), normalizedUsername);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return std::nullopt;
	}
	if (!query.next()) {
		setError(error, QStringLiteral("Profile not found"));
		return std::nullopt;
	}
	PlayerProfile result = profileFromQuery(query);
	addProfileRank(&result);
	return result;
}

void AccountStore::addProfileRank(PlayerProfile* profile)
{
	if (!profile)
		return;
	profile->leaderboardRank = 0;
	static const int thresholds[] = {
		0, 100, 250, 500, 900, 1400, 2000, 2800, 3800, 5000,
		6500, 8200, 10200, 12500, 15100, 18000, 21200, 24700, 28500, 32600};
	static const QString drawingRanks[] = {
		QStringLiteral("Pencil I"), QStringLiteral("Pencil II"), QStringLiteral("Pencil III"),
		QStringLiteral("Sketcher I"), QStringLiteral("Sketcher II"), QStringLiteral("Sketcher III"),
		QStringLiteral("Doodler I"), QStringLiteral("Doodler II"), QStringLiteral("Doodler III"),
		QStringLiteral("Illustrator I"), QStringLiteral("Illustrator II"), QStringLiteral("Illustrator III"),
		QStringLiteral("Artist I"), QStringLiteral("Artist II"), QStringLiteral("Artist III"),
		QStringLiteral("Virtuoso I"), QStringLiteral("Virtuoso II"), QStringLiteral("Virtuoso III"),
		QStringLiteral("Master Artist"), QStringLiteral("Grandmaster Artist")};
	static const QString guessingRanks[] = {
		QStringLiteral("Word Seeker I"), QStringLiteral("Word Seeker II"), QStringLiteral("Word Seeker III"),
		QStringLiteral("Clue Finder I"), QStringLiteral("Clue Finder II"), QStringLiteral("Clue Finder III"),
		QStringLiteral("Phrase Tracker I"), QStringLiteral("Phrase Tracker II"), QStringLiteral("Phrase Tracker III"),
		QStringLiteral("Word Sleuth I"), QStringLiteral("Word Sleuth II"), QStringLiteral("Word Sleuth III"),
		QStringLiteral("Lexicon Scout I"), QStringLiteral("Lexicon Scout II"), QStringLiteral("Lexicon Scout III"),
		QStringLiteral("Vocabulary Virtuoso I"), QStringLiteral("Vocabulary Virtuoso II"), QStringLiteral("Vocabulary Virtuoso III"),
		QStringLiteral("Master Wordsmith"), QStringLiteral("Grandmaster Wordsmith")};
	const auto rankForScore = [](int score, const QString* rankNames) {
		int tier = 0;
		for (int index = 1; index < static_cast<int>(std::size(thresholds)); ++index) {
			if (score < thresholds[index])
				break;
			tier = index;
		}
		return rankNames[tier];
	};
	profile->guessingRank = rankForScore(profile->guessingScore, guessingRanks);
	profile->drawingRank = rankForScore(profile->drawingScore, drawingRanks);

	if (profile->totalScore > 0) {
		QSqlQuery query(database_);
		query.prepare(QStringLiteral(
			"SELECT COUNT(*) + 1 FROM accounts "
			"WHERE guessing_score + drawing_score > :score "
			"OR (guessing_score + drawing_score = :score AND score_order < "
			"(SELECT score_order FROM accounts WHERE username = :username))"));
		query.bindValue(QStringLiteral(":score"), profile->totalScore);
		query.bindValue(QStringLiteral(":username"), profile->username);
		if (query.exec() && query.next())
			profile->leaderboardRank = query.value(0).toInt();
	}

	const auto trackRank = [this, profile](const QString& column, const QString& orderColumn) {
		QSqlQuery rankQuery(database_);
		rankQuery.prepare(QStringLiteral(
			"SELECT COUNT(*) + 1 FROM accounts "
			"WHERE %1 > (SELECT %1 FROM accounts WHERE username = :username) "
			"OR (%1 = (SELECT %1 FROM accounts WHERE username = :username) AND %2 < "
			"(SELECT %2 FROM accounts WHERE username = :username))").arg(column, orderColumn));
		rankQuery.bindValue(QStringLiteral(":username"), profile->username);
		return rankQuery.exec() && rankQuery.next() ? rankQuery.value(0).toInt() : 0;
	};
	profile->guessingLeaderboardRank = profile->guessingScore > 0
		? trackRank(QStringLiteral("guessing_score"), QStringLiteral("guessing_order")) : 0;
	profile->drawingLeaderboardRank = profile->drawingScore > 0
		? trackRank(QStringLiteral("drawing_score"), QStringLiteral("drawing_order")) : 0;
}

bool AccountStore::addScore(const QString& username, int points, bool drawingPoints, QString* error)
{
	if (points < 0 || points > 10000) {
		setError(error, QStringLiteral("Score is outside the allowed range"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(drawingPoints
		? QStringLiteral("UPDATE accounts SET drawing_score = drawing_score + :points, "
			"score_order = CASE WHEN :points > 0 THEN (SELECT COALESCE(MAX(score_order), 0) + 1 FROM accounts) "
			"ELSE score_order END, drawing_order = CASE WHEN :points = 0 THEN drawing_order "
			"ELSE (SELECT COALESCE(MAX(drawing_order), 0) + 1 FROM accounts) "
			"END WHERE username = :username")
		: QStringLiteral("UPDATE accounts SET guessing_score = guessing_score + :points, "
			"score_order = CASE WHEN :points > 0 THEN (SELECT COALESCE(MAX(score_order), 0) + 1 FROM accounts) "
			"ELSE score_order END, guessing_order = CASE WHEN :points > 0 "
			"THEN (SELECT COALESCE(MAX(guessing_order), 0) + 1 FROM accounts) "
			"ELSE guessing_order END WHERE username = :username"));
	query.bindValue(QStringLiteral(":points"), points);
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
							? QStringLiteral("Profile not found")
							: query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::recordRoundResult(const QString& username, bool roundWon, QString* error)
{
	QSqlQuery query(database_);
	query.prepare(roundWon
		? QStringLiteral("UPDATE accounts SET rounds_won = rounds_won + 1 WHERE username = :username")
		: QStringLiteral("UPDATE accounts SET rounds_lost = rounds_lost + 1 WHERE username = :username"));
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Profile not found") : query.lastError().text());
		return false;
	}
	return true;
}

}
