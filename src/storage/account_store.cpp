#include "storage/account_store.h"
#include "shared/protocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <iterator>

namespace gtd {
namespace {

constexpr int PasswordIterations = 210000;
constexpr qsizetype PasswordHashBytes = 32;

QString normalizedUsername(const QString& username)
{
	return username.trimmed().toLower();
}

bool validModerationIdentity(const QString& value)
{
	static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_]{3,24}$"));
	return pattern.match(value.trimmed()).hasMatch();
}

bool validServerId(const QString& value)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
	return pattern.match(value).hasMatch();
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
	profile.roundsAbandoned = query.value(8).toInt();
	profile.masterSuperAdmin = query.value(9).toBool();
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
	if (!query.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN rounds_abandoned INTEGER NOT NULL DEFAULT 0 CHECK(rounds_abandoned >= 0)"))
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
	const QStringList moderationTables{
		QStringLiteral("CREATE TABLE IF NOT EXISTS master_super_admins ("
			"username TEXT PRIMARY KEY NOT NULL REFERENCES accounts(username) ON DELETE CASCADE)"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS server_staff ("
			"server_id TEXT NOT NULL, username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"role TEXT NOT NULL CHECK(role IN ('admin', 'moderator')), "
			"assigned_by TEXT NOT NULL REFERENCES accounts(username), assigned_at INTEGER NOT NULL, "
			"PRIMARY KEY(server_id, username))"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS server_sanctions ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, server_id TEXT NOT NULL, "
			"target_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"kind TEXT NOT NULL CHECK(kind IN ('ban', 'mute')), "
			"text_chat INTEGER NOT NULL DEFAULT 0 CHECK(text_chat IN (0, 1)), "
			"voice_chat INTEGER NOT NULL DEFAULT 0 CHECK(voice_chat IN (0, 1)), "
			"reason TEXT NOT NULL, issued_by TEXT NOT NULL REFERENCES accounts(username), "
			"created_at INTEGER NOT NULL, expires_at INTEGER, revoked_by TEXT REFERENCES accounts(username), "
			"revoked_at INTEGER)"),
		QStringLiteral("CREATE INDEX IF NOT EXISTS server_sanctions_active "
			"ON server_sanctions(server_id, target_username, kind, revoked_at, expires_at)"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS player_mutes ("
			"server_id TEXT NOT NULL, muter_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"target_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"text_chat INTEGER NOT NULL CHECK(text_chat IN (0, 1)), "
			"voice_chat INTEGER NOT NULL CHECK(voice_chat IN (0, 1)), updated_at INTEGER NOT NULL, "
			"CHECK(text_chat = 1 OR voice_chat = 1), "
			"CHECK(muter_username <> target_username), PRIMARY KEY(server_id, muter_username, target_username))"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS friendships ("
			"user_a TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"user_b TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"created_at INTEGER NOT NULL, CHECK(user_a < user_b), PRIMARY KEY(user_a, user_b))"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS friend_requests ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, sender_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"recipient_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"status TEXT NOT NULL CHECK(status IN ('pending', 'denied')), created_at INTEGER NOT NULL, "
			"CHECK(sender_username <> recipient_username), UNIQUE(sender_username, recipient_username))"),
		QStringLiteral("CREATE INDEX IF NOT EXISTS friend_requests_by_recipient "
			"ON friend_requests(recipient_username, status, created_at)"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS user_blocks ("
			"blocker_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"target_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"created_at INTEGER NOT NULL, CHECK(blocker_username <> target_username), "
			"PRIMARY KEY(blocker_username, target_username))"),
		QStringLiteral("CREATE INDEX IF NOT EXISTS user_blocks_by_target ON user_blocks(target_username, blocker_username)"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS direct_messages ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, sender_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"recipient_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"body TEXT NOT NULL CHECK(length(body) BETWEEN 1 AND 2000), created_at INTEGER NOT NULL, "
			"CHECK(sender_username <> recipient_username))"),
		QStringLiteral("CREATE INDEX IF NOT EXISTS direct_messages_between_users "
			"ON direct_messages(sender_username, recipient_username, id)"),
		QStringLiteral("CREATE INDEX IF NOT EXISTS direct_messages_inbox "
			"ON direct_messages(recipient_username, id)"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS direct_message_reads ("
			"username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"other_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"last_read_id INTEGER NOT NULL DEFAULT 0 CHECK(last_read_id >= 0), "
			"CHECK(username <> other_username), PRIMARY KEY(username, other_username))"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS direct_message_hides ("
			"username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"message_id INTEGER NOT NULL REFERENCES direct_messages(id) ON DELETE CASCADE, "
			"PRIMARY KEY(username, message_id))"),
		QStringLiteral("CREATE TABLE IF NOT EXISTS direct_message_conversation_hides ("
			"username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"other_username TEXT NOT NULL REFERENCES accounts(username) ON DELETE CASCADE, "
			"hidden_through_id INTEGER NOT NULL DEFAULT 0 CHECK(hidden_through_id >= 0), "
			"CHECK(username <> other_username), PRIMARY KEY(username, other_username))")};
	for (const QString& statement : moderationTables) {
		if (!query.exec(statement)) {
			setError(error, query.lastError().text());
			return false;
		}
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
	const QString safeDisplayName = displayName.trimmed();
	if (!isValidSingleLineText(safeDisplayName, 32)) {
		setError(error, QStringLiteral("Display name must be 1-32 characters without control characters"));
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
	static const QRegularExpression usernamePattern(QStringLiteral("^[a-zA-Z0-9_]{3,24}$"));
	if (!usernamePattern.match(username.trimmed()).hasMatch() || password.size() < 8 || password.size() > 128) {
		setError(error, QStringLiteral("Invalid username or password"));
		return std::nullopt;
	}
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

QList<PlayerProfile> AccountStore::searchProfiles(const QString& text, int limit, QString* error)
{
	const QString search = text.trimmed();
	if (search.isEmpty() || search.size() > 64 || !isValidSingleLineText(search, 64)) {
		setError(error, QStringLiteral("Enter a valid user search"));
		return {};
	}
	QString escaped = search;
	escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
	escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT username FROM accounts WHERE username LIKE :pattern ESCAPE '\\' "
		"OR display_name LIKE :pattern ESCAPE '\\' "
		"ORDER BY CASE WHEN username = :exact THEN 0 ELSE 1 END, username LIMIT :limit"));
	query.bindValue(QStringLiteral(":pattern"), QStringLiteral("%") + escaped + QStringLiteral("%"));
	query.bindValue(QStringLiteral(":exact"), normalizedUsername(search));
	query.bindValue(QStringLiteral(":limit"), qBound(1, limit, MaximumUserSearchResults));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<PlayerProfile> result;
	while (query.next()) {
		auto found = loadProfile(query.value(0).toString(), error);
		if (!found)
			return {};
		result.append(*found);
	}
	return result;
}

QList<ServerSanction> AccountStore::activeBansForUser(const QString& username, QString* error)
{
	const QString target = normalizedUsername(username);
	if (!validModerationIdentity(target)) {
		setError(error, QStringLiteral("Invalid profile username"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT id, server_id, target_username, kind, text_chat, voice_chat, reason, issued_by, created_at, "
		"COALESCE(expires_at, 0) FROM server_sanctions WHERE target_username = :target AND kind = 'ban' "
		"AND revoked_at IS NULL AND (expires_at IS NULL OR expires_at > :now) ORDER BY server_id"));
	query.bindValue(QStringLiteral(":target"), target);
	query.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<ServerSanction> result;
	while (query.next()) {
		ServerSanction sanction;
		sanction.id = query.value(0).toLongLong();
		sanction.serverId = query.value(1).toString();
		sanction.targetUsername = query.value(2).toString();
		sanction.kind = query.value(3).toString();
		sanction.textChat = query.value(4).toBool();
		sanction.voiceChat = query.value(5).toBool();
		sanction.reason = query.value(6).toString();
		sanction.issuedBy = query.value(7).toString();
		sanction.createdAt = query.value(8).toLongLong();
		sanction.expiresAt = query.value(9).toLongLong();
		result.append(sanction);
	}
	return result;
}

QList<QString> AccountStore::friends(const QString& username, QString* error)
{
	const QString user = normalizedUsername(username);
	if (!validModerationIdentity(user)) {
		setError(error, QStringLiteral("Invalid account username"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT CASE WHEN user_a = :user THEN user_b ELSE user_a END FROM friendships "
		"WHERE user_a = :user OR user_b = :user ORDER BY created_at DESC LIMIT :limit"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":limit"), MaximumSocialListItems);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<QString> result;
	while (query.next())
		result.append(query.value(0).toString());
	return result;
}

QList<FriendRequest> AccountStore::friendRequests(const QString& username, QString* error)
{
	const QString user = normalizedUsername(username);
	if (!validModerationIdentity(user)) {
		setError(error, QStringLiteral("Invalid account username"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT id, sender_username, recipient_username, status, created_at FROM friend_requests "
		"WHERE (recipient_username = :user AND status = 'pending') "
		"OR (sender_username = :user AND status IN ('pending', 'denied')) "
		"ORDER BY created_at DESC LIMIT :limit"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":limit"), MaximumSocialListItems);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<FriendRequest> result;
	while (query.next()) {
		FriendRequest request;
		request.id = query.value(0).toLongLong();
		request.senderUsername = query.value(1).toString();
		request.recipientUsername = query.value(2).toString();
		request.status = query.value(3).toString();
		request.createdAt = query.value(4).toLongLong();
		result.append(request);
	}
	return result;
}

QList<QString> AccountStore::blockedUsers(const QString& username, QString* error)
{
	const QString user = normalizedUsername(username);
	if (!validModerationIdentity(user)) {
		setError(error, QStringLiteral("Invalid account username"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT target_username FROM user_blocks WHERE blocker_username = :user "
		"ORDER BY created_at DESC LIMIT :limit"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":limit"), MaximumSocialListItems);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<QString> result;
	while (query.next())
		result.append(query.value(0).toString());
	return result;
}

bool AccountStore::areFriends(const QString& firstUsername, const QString& secondUsername, QString* error)
{
	const QString first = normalizedUsername(firstUsername);
	const QString second = normalizedUsername(secondUsername);
	if (!validModerationIdentity(first) || !validModerationIdentity(second) || first == second) {
		setError(error, QStringLiteral("Invalid friendship query"));
		return false;
	}
	const QString userA = first < second ? first : second;
	const QString userB = first < second ? second : first;
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("SELECT 1 FROM friendships WHERE user_a = :first AND user_b = :second"));
	query.bindValue(QStringLiteral(":first"), userA);
	query.bindValue(QStringLiteral(":second"), userB);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return query.next();
}

bool AccountStore::isBlocked(const QString& firstUsername, const QString& secondUsername, QString* error)
{
	const QString first = normalizedUsername(firstUsername);
	const QString second = normalizedUsername(secondUsername);
	if (!validModerationIdentity(first) || !validModerationIdentity(second) || first == second) {
		setError(error, QStringLiteral("Invalid block query"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT 1 FROM user_blocks WHERE (blocker_username = :first AND target_username = :second) "
		"OR (blocker_username = :second AND target_username = :first)"));
	query.bindValue(QStringLiteral(":first"), first);
	query.bindValue(QStringLiteral(":second"), second);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return query.next();
}

bool AccountStore::sendFriendRequest(const QString& senderUsername, const QString& recipientUsername,
	qint64* requestId, QString* error)
{
	const QString sender = normalizedUsername(senderUsername);
	const QString recipient = normalizedUsername(recipientUsername);
	if (!validModerationIdentity(sender) || !validModerationIdentity(recipient) || sender == recipient) {
		setError(error, QStringLiteral("Invalid friend request"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	const auto rollbackWith = [this, error](const QString& message) {
		database_.rollback();
		setError(error, message);
		return false;
	};
	if (!profile(recipient, error)) {
		database_.rollback();
		return false;
	}
	if (isBlocked(sender, recipient, error))
		return rollbackWith(QStringLiteral("Friend requests are unavailable for this user"));
	if (error && !error->isEmpty()) {
		database_.rollback();
		return false;
	}
	if (areFriends(sender, recipient, error))
		return rollbackWith(QStringLiteral("You are already friends"));
	if (error && !error->isEmpty()) {
		database_.rollback();
		return false;
	}
	QSqlQuery existing(database_);
	existing.prepare(QStringLiteral(
		"SELECT status FROM friend_requests WHERE sender_username = :sender AND recipient_username = :recipient"));
	existing.bindValue(QStringLiteral(":sender"), sender);
	existing.bindValue(QStringLiteral(":recipient"), recipient);
	if (!existing.exec())
		return rollbackWith(existing.lastError().text());
	if (existing.next())
		return rollbackWith(existing.value(0).toString() == QStringLiteral("denied")
			? QStringLiteral("Clear the denied request before sending another")
			: QStringLiteral("Friend request is already pending"));
	QSqlQuery reverse(database_);
	reverse.prepare(QStringLiteral(
		"SELECT 1 FROM friend_requests WHERE sender_username = :recipient "
		"AND recipient_username = :sender AND status = 'pending'"));
	reverse.bindValue(QStringLiteral(":sender"), sender);
	reverse.bindValue(QStringLiteral(":recipient"), recipient);
	if (!reverse.exec())
		return rollbackWith(reverse.lastError().text());
	if (reverse.next())
		return rollbackWith(QStringLiteral("This user has already sent you a friend request"));
	QSqlQuery insert(database_);
	insert.prepare(QStringLiteral(
		"INSERT INTO friend_requests(sender_username, recipient_username, status, created_at) "
		"VALUES(:sender, :recipient, 'pending', :now)"));
	insert.bindValue(QStringLiteral(":sender"), sender);
	insert.bindValue(QStringLiteral(":recipient"), recipient);
	insert.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	if (!insert.exec() || !database_.commit())
		return rollbackWith(insert.lastError().text().isEmpty() ? database_.lastError().text() : insert.lastError().text());
	if (requestId)
		*requestId = insert.lastInsertId().toLongLong();
	return true;
}

bool AccountStore::acceptFriendRequest(const QString& recipientUsername, qint64 requestId, QString* error)
{
	const QString recipient = normalizedUsername(recipientUsername);
	if (!validModerationIdentity(recipient) || requestId <= 0) {
		setError(error, QStringLiteral("Invalid friend request"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QSqlQuery request(database_);
	request.prepare(QStringLiteral(
		"SELECT sender_username FROM friend_requests WHERE id = :id AND recipient_username = :recipient "
		"AND status = 'pending'"));
	request.bindValue(QStringLiteral(":id"), requestId);
	request.bindValue(QStringLiteral(":recipient"), recipient);
	if (!request.exec() || !request.next()) {
		database_.rollback();
		setError(error, request.lastError().text().isEmpty()
			? QStringLiteral("Friend request is no longer available") : request.lastError().text());
		return false;
	}
	const QString sender = request.value(0).toString();
	if (isBlocked(sender, recipient, error)) {
		database_.rollback();
		setError(error, QStringLiteral("Friend requests are unavailable for this user"));
		return false;
	}
	if (error && !error->isEmpty()) {
		database_.rollback();
		return false;
	}
	const QString userA = sender < recipient ? sender : recipient;
	const QString userB = sender < recipient ? recipient : sender;
	QSqlQuery insert(database_);
	insert.prepare(QStringLiteral("INSERT OR IGNORE INTO friendships(user_a, user_b, created_at) VALUES(:a, :b, :now)"));
	insert.bindValue(QStringLiteral(":a"), userA);
	insert.bindValue(QStringLiteral(":b"), userB);
	insert.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	QSqlQuery remove(database_);
	remove.prepare(QStringLiteral("DELETE FROM friend_requests WHERE id = :id"));
	remove.bindValue(QStringLiteral(":id"), requestId);
	if (!insert.exec() || !remove.exec() || !database_.commit()) {
		const QString message = !insert.lastError().text().isEmpty() ? insert.lastError().text()
			: (!remove.lastError().text().isEmpty() ? remove.lastError().text() : database_.lastError().text());
		database_.rollback();
		setError(error, message);
		return false;
	}
	return true;
}

bool AccountStore::denyFriendRequest(const QString& recipientUsername, qint64 requestId, QString* error)
{
	const QString recipient = normalizedUsername(recipientUsername);
	if (!validModerationIdentity(recipient) || requestId <= 0) {
		setError(error, QStringLiteral("Invalid friend request"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"UPDATE friend_requests SET status = 'denied' WHERE id = :id AND recipient_username = :recipient "
		"AND status = 'pending'"));
	query.bindValue(QStringLiteral(":id"), requestId);
	query.bindValue(QStringLiteral(":recipient"), recipient);
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Friend request is no longer available") : query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::clearDeniedFriendRequest(const QString& senderUsername, qint64 requestId, QString* error)
{
	const QString sender = normalizedUsername(senderUsername);
	if (!validModerationIdentity(sender) || requestId <= 0) {
		setError(error, QStringLiteral("Invalid friend request"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"DELETE FROM friend_requests WHERE id = :id AND sender_username = :sender AND status = 'denied'"));
	query.bindValue(QStringLiteral(":id"), requestId);
	query.bindValue(QStringLiteral(":sender"), sender);
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Denied friend request is no longer available") : query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::removeFriend(const QString& username, const QString& friendUsername, QString* error)
{
	const QString user = normalizedUsername(username);
	const QString other = normalizedUsername(friendUsername);
	if (!validModerationIdentity(user) || !validModerationIdentity(other) || user == other) {
		setError(error, QStringLiteral("Invalid friendship"));
		return false;
	}
	const QString userA = user < other ? user : other;
	const QString userB = user < other ? other : user;
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("DELETE FROM friendships WHERE user_a = :a AND user_b = :b"));
	query.bindValue(QStringLiteral(":a"), userA);
	query.bindValue(QStringLiteral(":b"), userB);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::setBlocked(const QString& blockerUsername, const QString& targetUsername, bool blocked,
	QString* error)
{
	const QString blocker = normalizedUsername(blockerUsername);
	const QString target = normalizedUsername(targetUsername);
	if (!validModerationIdentity(blocker) || !validModerationIdentity(target) || blocker == target) {
		setError(error, QStringLiteral("Invalid block request"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QSqlQuery change(database_);
	change.prepare(blocked
		? QStringLiteral("INSERT OR IGNORE INTO user_blocks(blocker_username, target_username, created_at) "
			"VALUES(:blocker, :target, :now)")
		: QStringLiteral("DELETE FROM user_blocks WHERE blocker_username = :blocker AND target_username = :target"));
	change.bindValue(QStringLiteral(":blocker"), blocker);
	change.bindValue(QStringLiteral(":target"), target);
	if (blocked)
		change.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	if (!change.exec()) {
		database_.rollback();
		setError(error, change.lastError().text());
		return false;
	}
	if (blocked) {
		const QString userA = blocker < target ? blocker : target;
		const QString userB = blocker < target ? target : blocker;
		QSqlQuery friendship(database_);
		friendship.prepare(QStringLiteral("DELETE FROM friendships WHERE user_a = :a AND user_b = :b"));
		friendship.bindValue(QStringLiteral(":a"), userA);
		friendship.bindValue(QStringLiteral(":b"), userB);
		QSqlQuery requests(database_);
		requests.prepare(QStringLiteral(
			"DELETE FROM friend_requests WHERE status = 'pending' AND "
			"((sender_username = :blocker AND recipient_username = :target) "
			"OR (sender_username = :target AND recipient_username = :blocker))"));
		requests.bindValue(QStringLiteral(":blocker"), blocker);
		requests.bindValue(QStringLiteral(":target"), target);
		if (!friendship.exec() || !requests.exec()) {
			const QString message = !friendship.lastError().text().isEmpty()
				? friendship.lastError().text() : requests.lastError().text();
			database_.rollback();
			setError(error, message);
			return false;
		}
	}
	if (!database_.commit()) {
		setError(error, database_.lastError().text());
		database_.rollback();
		return false;
	}
	return true;
}

QList<QString> AccountStore::inboxContacts(const QString& username, QString* error)
{
	const QString user = normalizedUsername(username);
	if (!validModerationIdentity(user)) {
		setError(error, QStringLiteral("Invalid account username"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT CASE WHEN m.sender_username = :user THEN m.recipient_username ELSE m.sender_username END AS other_user, "
		"MAX(id) AS last_message FROM direct_messages m WHERE (sender_username = :user OR recipient_username = :user) "
		"AND id > COALESCE((SELECT hidden_through_id FROM direct_message_conversation_hides h "
		"WHERE h.username = :user AND h.other_username = CASE WHEN m.sender_username = :user "
		"THEN m.recipient_username ELSE m.sender_username END), 0) "
		"AND NOT EXISTS (SELECT 1 FROM direct_message_hides dmh WHERE dmh.username = :user AND dmh.message_id = m.id) "
		"GROUP BY other_user ORDER BY last_message DESC LIMIT :limit"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":limit"), MaximumSocialListItems);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<QString> result;
	while (query.next()) {
		const QString other = query.value(0).toString();
		QString blockError;
		if (!isBlocked(user, other, &blockError)) {
			if (!blockError.isEmpty()) {
				setError(error, blockError);
				return {};
			}
			result.append(other);
		}
	}
	return result;
}

bool AccountStore::deleteInboxConversation(const QString& username, const QString& otherUsername, QString* error)
{
	const QString user = normalizedUsername(username);
	const QString other = normalizedUsername(otherUsername);
	if (!validModerationIdentity(user) || !validModerationIdentity(other) || user == other) {
		setError(error, QStringLiteral("Invalid inbox conversation"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QSqlQuery hide(database_);
	hide.prepare(QStringLiteral(
		"INSERT INTO direct_message_conversation_hides(username, other_username, hidden_through_id) "
		"SELECT :user, :other, COALESCE(MAX(id), 0) FROM direct_messages "
		"WHERE (sender_username = :user AND recipient_username = :other) "
		"OR (sender_username = :other AND recipient_username = :user) "
		"ON CONFLICT(username, other_username) DO UPDATE SET hidden_through_id = "
		"MAX(hidden_through_id, excluded.hidden_through_id)"));
	hide.bindValue(QStringLiteral(":user"), user);
	hide.bindValue(QStringLiteral(":other"), other);
	if (!hide.exec() || !database_.commit()) {
		database_.rollback();
		setError(error, hide.lastError().text().isEmpty() ? database_.lastError().text() : hide.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::deleteDirectMessage(const QString& username, qint64 messageId, QString* error)
{
	const QString user = normalizedUsername(username);
	if (!validModerationIdentity(user) || messageId <= 0) {
		setError(error, QStringLiteral("Invalid direct message"));
		return false;
	}
	QSqlQuery hide(database_);
	hide.prepare(QStringLiteral(
		"INSERT INTO direct_message_hides(username, message_id) "
		"SELECT :user, id FROM direct_messages WHERE id = :id "
		"AND (sender_username = :user OR recipient_username = :user) "
		"ON CONFLICT(username, message_id) DO NOTHING"));
	hide.bindValue(QStringLiteral(":user"), user);
	hide.bindValue(QStringLiteral(":id"), messageId);
	if (!hide.exec() || hide.numRowsAffected() != 1) {
		setError(error, hide.lastError().text().isEmpty()
			? QStringLiteral("Message not found") : hide.lastError().text());
		return false;
	}
	return true;
}

int AccountStore::unreadDirectMessageCount(const QString& username, const QString& otherUsername, QString* error)
{
	const QString user = normalizedUsername(username);
	const QString other = normalizedUsername(otherUsername);
	if (!validModerationIdentity(user) || !validModerationIdentity(other) || user == other) {
		setError(error, QStringLiteral("Invalid inbox query"));
		return 0;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT COUNT(*) FROM direct_messages m WHERE recipient_username = :user AND sender_username = :other "
		"AND id > COALESCE((SELECT last_read_id FROM direct_message_reads "
		"WHERE username = :user AND other_username = :other), 0) "
		"AND id > COALESCE((SELECT hidden_through_id FROM direct_message_conversation_hides "
		"WHERE username = :user AND other_username = :other), 0) "
		"AND NOT EXISTS (SELECT 1 FROM direct_message_hides WHERE username = :user AND message_id = m.id)"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":other"), other);
	if (!query.exec() || !query.next()) {
		setError(error, query.lastError().text());
		return 0;
	}
	return query.value(0).toInt();
}

bool AccountStore::markDirectMessagesRead(const QString& username, const QString& otherUsername, QString* error)
{
	const QString user = normalizedUsername(username);
	const QString other = normalizedUsername(otherUsername);
	if (!validModerationIdentity(user) || !validModerationIdentity(other) || user == other) {
		setError(error, QStringLiteral("Invalid inbox query"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QSqlQuery update(database_);
	update.prepare(QStringLiteral(
		"INSERT INTO direct_message_reads(username, other_username, last_read_id) "
		"VALUES(:user, :other, COALESCE((SELECT MAX(id) FROM direct_messages "
		"WHERE recipient_username = :user AND sender_username = :other), 0)) "
		"ON CONFLICT(username, other_username) DO UPDATE SET last_read_id = MAX(last_read_id, excluded.last_read_id)"));
	update.bindValue(QStringLiteral(":user"), user);
	update.bindValue(QStringLiteral(":other"), other);
	if (!update.exec() || !database_.commit()) {
		database_.rollback();
		setError(error, update.lastError().text().isEmpty() ? database_.lastError().text() : update.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::sendDirectMessage(const QString& senderUsername, const QString& recipientUsername,
	const QString& body, qint64* messageId, QString* error)
{
	const QString sender = normalizedUsername(senderUsername);
	const QString recipient = normalizedUsername(recipientUsername);
	const QString messageBody = body.trimmed();
	if (!validModerationIdentity(sender) || !validModerationIdentity(recipient) || sender == recipient
		|| messageBody.size() > MaximumDirectMessageLength
		|| !isValidSingleLineText(messageBody, MaximumDirectMessageLength)) {
		setError(error, QStringLiteral("Invalid private message"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	const bool blocked = isBlocked(sender, recipient, error);
	if ((error && !error->isEmpty()) || blocked) {
		database_.rollback();
		if (blocked)
			setError(error, QStringLiteral("Private messaging is unavailable for this user"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"INSERT INTO direct_messages(sender_username, recipient_username, body, created_at) "
		"VALUES(:sender, :recipient, :body, :now)"));
	query.bindValue(QStringLiteral(":sender"), sender);
	query.bindValue(QStringLiteral(":recipient"), recipient);
	query.bindValue(QStringLiteral(":body"), messageBody);
	query.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	if (!query.exec() || !database_.commit()) {
		const QString message = query.lastError().text().isEmpty() ? database_.lastError().text() : query.lastError().text();
		database_.rollback();
		setError(error, message);
		return false;
	}
	if (messageId)
		*messageId = query.lastInsertId().toLongLong();
	return true;
}

QList<DirectMessage> AccountStore::directMessageHistory(const QString& username, const QString& otherUsername,
	qint64 beforeId, int limit, QString* error)
{
	const QString user = normalizedUsername(username);
	const QString other = normalizedUsername(otherUsername);
	if (!validModerationIdentity(user) || !validModerationIdentity(other) || user == other || beforeId < 0) {
		setError(error, QStringLiteral("Invalid inbox query"));
		return {};
	}
	QString blockError;
	if (isBlocked(user, other, &blockError)) {
		setError(error, QStringLiteral("Private messaging is unavailable for this user"));
		return {};
	}
	if (!blockError.isEmpty()) {
		setError(error, blockError);
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT id, sender_username, recipient_username, body, created_at FROM direct_messages m "
		"WHERE ((sender_username = :user AND recipient_username = :other) "
		"OR (sender_username = :other AND recipient_username = :user)) "
		"AND id > COALESCE((SELECT hidden_through_id FROM direct_message_conversation_hides "
		"WHERE username = :user AND other_username = :other), 0) "
		"AND NOT EXISTS (SELECT 1 FROM direct_message_hides WHERE username = :user AND message_id = m.id) "
		"AND (:before = 0 OR id < :before) ORDER BY id DESC LIMIT :limit"));
	query.bindValue(QStringLiteral(":user"), user);
	query.bindValue(QStringLiteral(":other"), other);
	query.bindValue(QStringLiteral(":before"), beforeId);
	query.bindValue(QStringLiteral(":limit"), qBound(1, limit, MaximumInboxHistory));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<DirectMessage> result;
	while (query.next()) {
		DirectMessage message;
		message.id = query.value(0).toLongLong();
		message.senderUsername = query.value(1).toString();
		message.recipientUsername = query.value(2).toString();
		message.body = query.value(3).toString();
		message.createdAt = query.value(4).toLongLong();
		result.append(message);
	}
	std::reverse(result.begin(), result.end());
	return result;
}

bool AccountStore::updateCountry(const QString& username, const QString& countryCode, QString* error)
{
	const QString normalizedCountryCode = countryCode.trimmed().toUpper();
	static const QStringList validCountryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FO FR GA GB GB-ENG GB-NIR GB-SCT GB-WLS GD GE GF GG GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT HU "
		"ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS LT "
		"LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI NL "
		"NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG SH "
		"SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG UM "
		"US UY UZ VA VC VE VG VI VN VU WF WS XK YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (!normalizedCountryCode.isEmpty() && !validCountryCodes.contains(normalizedCountryCode)) {
		setError(error, QStringLiteral("Choose a valid country code"));
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
	const QString safeDisplayName = displayName.trimmed();
	if (!isValidSingleLineText(safeDisplayName, 32)) {
		setError(error, QStringLiteral("Nickname must be 1-32 characters without control characters"));
		return false;
	}
	const QString normalizedCountryCode = countryCode.trimmed().toUpper();
	static const QStringList validCountryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FO FR GA GB GB-ENG GB-NIR GB-SCT GB-WLS GD GE GF GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT HU "
		"ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS LT "
		"LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI NL "
		"NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG SH "
		"SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG UM "
		"US UY UZ VA VC VE VG VI VN VU WF WS XK YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (!normalizedCountryCode.isEmpty() && !validCountryCodes.contains(normalizedCountryCode)) {
		setError(error, QStringLiteral("Choose a valid country code"));
		return false;
	}
	if (!newPassword.isEmpty() && (newPassword.size() < 8 || newPassword.size() > 128)) {
		setError(error, QStringLiteral("New password must be 8-128 characters"));
		return false;
	}
	if (!currentPassword.isEmpty() && (currentPassword.size() < 8 || currentPassword.size() > 128)) {
		setError(error, QStringLiteral("Current password must be 8-128 characters"));
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
		"SELECT username, display_name, country_code, avatar_url, guessing_score, drawing_score, rounds_won, rounds_lost, rounds_abandoned, "
		"EXISTS(SELECT 1 FROM master_super_admins WHERE username = accounts.username) "
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
		"SELECT username, display_name, country_code, avatar_url, guessing_score, drawing_score, rounds_won, rounds_lost, rounds_abandoned, "
		"EXISTS(SELECT 1 FROM master_super_admins WHERE username = accounts.username) "
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

bool AccountStore::recordRoundAbandonment(const QString& username, QString* error)
{
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("UPDATE accounts SET rounds_abandoned = rounds_abandoned + 1 WHERE username = :username"));
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Profile not found") : query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::isMasterSuperAdmin(const QString& username, QString* error)
{
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("SELECT 1 FROM master_super_admins WHERE username = :username"));
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return query.next();
}

QList<QString> AccountStore::masterSuperAdmins(QString* error)
{
	QSqlQuery query(database_);
	if (!query.exec(QStringLiteral("SELECT username FROM master_super_admins ORDER BY username"))) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<QString> result;
	while (query.next())
		result.append(query.value(0).toString());
	return result;
}

qint64 AccountStore::masterSuperAdminDataVersion(QString* error)
{
	QSqlQuery query(database_);
	if (!query.exec(QStringLiteral("PRAGMA data_version")) || !query.next()) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Could not read SQLite data version") : query.lastError().text());
		return -1;
	}
	return query.value(0).toLongLong();
}

QString AccountStore::serverRole(const QString& serverId, const QString& username, QString* error)
{
	if (!validServerId(serverId)) {
		setError(error, QStringLiteral("Invalid server ID"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("SELECT role FROM server_staff WHERE server_id = :server AND username = :username"));
	query.bindValue(QStringLiteral(":server"), serverId);
	query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	return query.next() ? query.value(0).toString() : QStringLiteral("player");
}

QList<ServerRoleAssignment> AccountStore::serverStaff(const QString& serverId, QString* error)
{
	if (!validServerId(serverId)) {
		setError(error, QStringLiteral("Invalid server ID"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT server_id, username, role, assigned_by FROM server_staff "
		"WHERE server_id = :server ORDER BY role, username"));
	query.bindValue(QStringLiteral(":server"), serverId);
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<ServerRoleAssignment> result;
	while (query.next()) {
		ServerRoleAssignment assignment;
		assignment.serverId = query.value(0).toString();
		assignment.username = query.value(1).toString();
		assignment.role = query.value(2).toString();
		assignment.assignedBy = query.value(3).toString();
		result.append(assignment);
	}
	return result;
}

bool AccountStore::setServerRole(const QString& serverId, const QString& username, const QString& role,
	const QString& assignedBy, QString* error)
{
	const QString target = normalizedUsername(username);
	const QString actor = normalizedUsername(assignedBy);
	if (!validServerId(serverId) || !validModerationIdentity(target) || !validModerationIdentity(actor)
		|| (role != QStringLiteral("admin") && role != QStringLiteral("moderator")
			&& role != QStringLiteral("player"))) {
		setError(error, QStringLiteral("Invalid server role assignment"));
		return false;
	}
	QSqlQuery query(database_);
	if (role == QStringLiteral("player")) {
		query.prepare(QStringLiteral("DELETE FROM server_staff WHERE server_id = :server AND username = :username"));
		query.bindValue(QStringLiteral(":server"), serverId);
		query.bindValue(QStringLiteral(":username"), target);
		if (!query.exec()) {
			setError(error, query.lastError().text());
			return false;
		}
		return true;
	}
	query.prepare(QStringLiteral(
		"INSERT INTO server_staff(server_id, username, role, assigned_by, assigned_at) "
		"VALUES(:server, :username, :role, :actor, :now) "
		"ON CONFLICT(server_id, username) DO UPDATE SET role = excluded.role, "
		"assigned_by = excluded.assigned_by, assigned_at = excluded.assigned_at"));
	query.bindValue(QStringLiteral(":server"), serverId);
	query.bindValue(QStringLiteral(":username"), target);
	query.bindValue(QStringLiteral(":role"), role);
	query.bindValue(QStringLiteral(":actor"), actor);
	query.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::addServerSanction(const ServerSanction& sanction, qint64* sanctionId, QString* error)
{
	const QString target = normalizedUsername(sanction.targetUsername);
	const QString actor = normalizedUsername(sanction.issuedBy);
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	if (!validServerId(sanction.serverId) || !validModerationIdentity(target)
		|| !validModerationIdentity(actor)
		|| (sanction.kind != QStringLiteral("ban") && sanction.kind != QStringLiteral("mute"))
		|| (sanction.kind == QStringLiteral("mute") && !sanction.textChat && !sanction.voiceChat)
		|| !isValidSingleLineText(sanction.reason.trimmed(), 500)
		|| (sanction.expiresAt != 0 && sanction.expiresAt <= now)) {
		setError(error, QStringLiteral("Invalid moderation action"));
		return false;
	}
	if (!database_.transaction()) {
		setError(error, database_.lastError().text());
		return false;
	}
	QSqlQuery revoke(database_);
	revoke.prepare(QStringLiteral(
		"UPDATE server_sanctions SET revoked_by = :actor, revoked_at = :now "
		"WHERE server_id = :server AND target_username = :target AND kind = :kind AND revoked_at IS NULL "
		"AND (expires_at IS NULL OR expires_at > :now)"));
	revoke.bindValue(QStringLiteral(":actor"), actor);
	revoke.bindValue(QStringLiteral(":now"), now);
	revoke.bindValue(QStringLiteral(":server"), sanction.serverId);
	revoke.bindValue(QStringLiteral(":target"), target);
	revoke.bindValue(QStringLiteral(":kind"), sanction.kind);
	QSqlQuery insert(database_);
	insert.prepare(QStringLiteral(
		"INSERT INTO server_sanctions(server_id, target_username, kind, text_chat, voice_chat, reason, "
		"issued_by, created_at, expires_at) VALUES(:server, :target, :kind, :text, :voice, :reason, :actor, :now, :expires)"));
	insert.bindValue(QStringLiteral(":server"), sanction.serverId);
	insert.bindValue(QStringLiteral(":target"), target);
	insert.bindValue(QStringLiteral(":kind"), sanction.kind);
	insert.bindValue(QStringLiteral(":text"), sanction.kind == QStringLiteral("mute") && sanction.textChat);
	insert.bindValue(QStringLiteral(":voice"), sanction.kind == QStringLiteral("mute") && sanction.voiceChat);
	insert.bindValue(QStringLiteral(":reason"), sanction.reason.trimmed());
	insert.bindValue(QStringLiteral(":actor"), actor);
	insert.bindValue(QStringLiteral(":now"), now);
	if (sanction.expiresAt == 0)
		insert.bindValue(QStringLiteral(":expires"), QVariant());
	else
		insert.bindValue(QStringLiteral(":expires"), sanction.expiresAt);
	if (!revoke.exec() || !insert.exec() || !database_.commit()) {
		const QString message = !revoke.lastError().text().isEmpty() ? revoke.lastError().text()
			: (!insert.lastError().text().isEmpty() ? insert.lastError().text() : database_.lastError().text());
		database_.rollback();
		setError(error, message);
		return false;
	}
	if (sanctionId)
		*sanctionId = insert.lastInsertId().toLongLong();
	return true;
}

QList<ServerSanction> AccountStore::activeServerSanctions(const QString& serverId,
	const QString& targetUsername, qint64 now, QString* error)
{
	if (!validServerId(serverId)) {
		setError(error, QStringLiteral("Invalid server ID"));
		return {};
	}
	if (now < 0)
		now = QDateTime::currentSecsSinceEpoch();
	const bool filterTarget = !targetUsername.isEmpty();
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT id, server_id, target_username, kind, text_chat, voice_chat, reason, issued_by, created_at, "
		"COALESCE(expires_at, 0) FROM server_sanctions WHERE server_id = :server AND revoked_at IS NULL "
		"AND (expires_at IS NULL OR expires_at > :now) %1 ORDER BY created_at DESC")
		.arg(filterTarget ? QStringLiteral("AND target_username = :target") : QString()));
	query.bindValue(QStringLiteral(":server"), serverId);
	query.bindValue(QStringLiteral(":now"), now);
	if (filterTarget)
		query.bindValue(QStringLiteral(":target"), normalizedUsername(targetUsername));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<ServerSanction> result;
	while (query.next()) {
		ServerSanction sanction;
		sanction.id = query.value(0).toLongLong();
		sanction.serverId = query.value(1).toString();
		sanction.targetUsername = query.value(2).toString();
		sanction.kind = query.value(3).toString();
		sanction.textChat = query.value(4).toBool();
		sanction.voiceChat = query.value(5).toBool();
		sanction.reason = query.value(6).toString();
		sanction.issuedBy = query.value(7).toString();
		sanction.createdAt = query.value(8).toLongLong();
		sanction.expiresAt = query.value(9).toLongLong();
		result.append(sanction);
	}
	return result;
}

bool AccountStore::revokeServerSanction(const QString& serverId, qint64 sanctionId,
	const QString& revokedBy, QString* error)
{
	if (!validServerId(serverId) || sanctionId <= 0 || !validModerationIdentity(revokedBy)) {
		setError(error, QStringLiteral("Invalid sanction revocation"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"UPDATE server_sanctions SET revoked_by = :actor, revoked_at = :now "
		"WHERE id = :id AND server_id = :server AND revoked_at IS NULL"));
	query.bindValue(QStringLiteral(":actor"), normalizedUsername(revokedBy));
	query.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	query.bindValue(QStringLiteral(":id"), sanctionId);
	query.bindValue(QStringLiteral(":server"), serverId);
	if (!query.exec() || query.numRowsAffected() != 1) {
		setError(error, query.lastError().text().isEmpty()
			? QStringLiteral("Active sanction not found") : query.lastError().text());
		return false;
	}
	return true;
}

bool AccountStore::setPlayerMute(const QString& serverId, const QString& muterUsername,
	const QString& targetUsername, bool textChat, bool voiceChat, QString* error)
{
	const QString muter = normalizedUsername(muterUsername);
	const QString target = normalizedUsername(targetUsername);
	if (!validServerId(serverId) || !validModerationIdentity(muter) || !validModerationIdentity(target)
		|| muter == target) {
		setError(error, QStringLiteral("Invalid player mute preference"));
		return false;
	}
	QSqlQuery query(database_);
	if (!textChat && !voiceChat) {
		query.prepare(QStringLiteral(
			"DELETE FROM player_mutes WHERE server_id = :server AND muter_username = :muter "
			"AND target_username = :target"));
		query.bindValue(QStringLiteral(":server"), serverId);
		query.bindValue(QStringLiteral(":muter"), muter);
		query.bindValue(QStringLiteral(":target"), target);
	} else {
		query.prepare(QStringLiteral(
			"INSERT INTO player_mutes(server_id, muter_username, target_username, text_chat, voice_chat, updated_at) "
			"VALUES(:server, :muter, :target, :text, :voice, :now) "
			"ON CONFLICT(server_id, muter_username, target_username) DO UPDATE SET "
			"text_chat = excluded.text_chat, voice_chat = excluded.voice_chat, updated_at = excluded.updated_at"));
		query.bindValue(QStringLiteral(":server"), serverId);
		query.bindValue(QStringLiteral(":muter"), muter);
		query.bindValue(QStringLiteral(":target"), target);
		query.bindValue(QStringLiteral(":text"), textChat);
		query.bindValue(QStringLiteral(":voice"), voiceChat);
		query.bindValue(QStringLiteral(":now"), QDateTime::currentSecsSinceEpoch());
	}
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return true;
}

QList<PlayerMutePreference> AccountStore::playerMutePreferences(const QString& serverId,
	const QString& muterUsername, QString* error)
{
	if (!validServerId(serverId) || !validModerationIdentity(muterUsername)) {
		setError(error, QStringLiteral("Invalid player mute query"));
		return {};
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral(
		"SELECT server_id, muter_username, target_username, text_chat, voice_chat FROM player_mutes "
		"WHERE server_id = :server AND muter_username = :muter ORDER BY target_username"));
	query.bindValue(QStringLiteral(":server"), serverId);
	query.bindValue(QStringLiteral(":muter"), normalizedUsername(muterUsername));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return {};
	}
	QList<PlayerMutePreference> result;
	while (query.next()) {
		PlayerMutePreference preference;
		preference.serverId = query.value(0).toString();
		preference.muterUsername = query.value(1).toString();
		preference.targetUsername = query.value(2).toString();
		preference.textChat = query.value(3).toBool();
		preference.voiceChat = query.value(4).toBool();
		result.append(preference);
	}
	return result;
}

bool AccountStore::clearPlayerMutePreferencesForTarget(const QString& serverId,
	const QString& targetUsername, QString* error)
{
	if (!validServerId(serverId) || !validModerationIdentity(targetUsername)) {
		setError(error, QStringLiteral("Invalid player mute target"));
		return false;
	}
	QSqlQuery query(database_);
	query.prepare(QStringLiteral("DELETE FROM player_mutes WHERE server_id = :server AND target_username = :target"));
	query.bindValue(QStringLiteral(":server"), serverId);
	query.bindValue(QStringLiteral(":target"), normalizedUsername(targetUsername));
	if (!query.exec()) {
		setError(error, query.lastError().text());
		return false;
	}
	return true;
}

}
