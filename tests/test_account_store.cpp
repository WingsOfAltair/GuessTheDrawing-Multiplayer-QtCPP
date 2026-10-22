#include "storage/account_store.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class AccountStoreTests final : public QObject {
	Q_OBJECT

private slots:
	void persistsFriendRequestsBlocksAndInbox()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		gtd::AccountStore store(directory.filePath(QStringLiteral("social.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		for (const QString& username : {QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol")})
			QVERIFY2(store.createAccount(username, QStringLiteral("password123"), username, &error), qPrintable(error));

		qint64 requestId = 0;
		QVERIFY2(store.sendFriendRequest(QStringLiteral("alice"), QStringLiteral("bob"), &requestId, &error), qPrintable(error));
		QCOMPARE(store.friendRequests(QStringLiteral("alice"), &error).size(), 1);
		QCOMPARE(store.friendRequests(QStringLiteral("bob"), &error).size(), 1);
		QVERIFY2(store.denyFriendRequest(QStringLiteral("bob"), requestId, &error), qPrintable(error));
		QCOMPARE(store.friendRequests(QStringLiteral("alice"), &error).first().status, QStringLiteral("denied"));
		QVERIFY2(store.addFriendRequestDecisionNotification(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("denied"), &error), qPrintable(error));
		QCOMPARE(store.unreadNotificationCount(QStringLiteral("alice"), &error), 1);
		QCOMPARE(store.notifications(QStringLiteral("alice"), &error).first().kind,
			QStringLiteral("friend_request_denied"));
		QVERIFY2(store.clearNotification(QStringLiteral("alice"),
			store.notifications(QStringLiteral("alice"), &error).first().id, &error), qPrintable(error));
		QCOMPARE(store.unreadNotificationCount(QStringLiteral("alice"), &error), 0);
		QVERIFY(store.friendRequests(QStringLiteral("bob"), &error).isEmpty());
		QVERIFY2(store.clearDeniedFriendRequest(QStringLiteral("alice"), requestId, &error), qPrintable(error));

		QVERIFY2(store.sendFriendRequest(QStringLiteral("alice"), QStringLiteral("bob"), &requestId, &error), qPrintable(error));
		QVERIFY2(store.acceptFriendRequest(QStringLiteral("bob"), requestId, &error), qPrintable(error));
		QVERIFY2(store.addFriendRequestDecisionNotification(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("accepted"), &error), qPrintable(error));
		QCOMPARE(store.notifications(QStringLiteral("alice"), &error).first().kind,
			QStringLiteral("friend_request_accepted"));
		QVERIFY2(store.addFriendAcceptedNotification(QStringLiteral("bob"), QStringLiteral("alice"), &error),
			qPrintable(error));
		QCOMPARE(store.notifications(QStringLiteral("bob"), &error).first().kind, QStringLiteral("friends_now"));
		QVERIFY2(store.addFriendRequestDecisionNotification(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("denied"), &error), qPrintable(error));
		QVERIFY2(store.clearFriendRequestNotifications(QStringLiteral("alice"), QStringLiteral("bob"), &error),
			qPrintable(error));
		QCOMPARE(store.notifications(QStringLiteral("alice"), &error).size(), 1);
		QVERIFY2(store.clearNotifications(QStringLiteral("bob"), &error), qPrintable(error));
		QCOMPARE(store.unreadNotificationCount(QStringLiteral("bob"), &error), 0);
		QVERIFY(store.areFriends(QStringLiteral("alice"), QStringLiteral("bob"), &error));
		QVERIFY2(store.sendDirectMessage(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("Hello"), nullptr, &error), qPrintable(error));
		QCOMPARE(store.directMessageHistory(QStringLiteral("bob"), QStringLiteral("alice"), 0, 10, &error).size(), 1);
		QCOMPARE(store.unreadDirectMessageCount(QStringLiteral("bob"), QStringLiteral("alice"), &error), 1);
		QVERIFY2(store.markDirectMessagesRead(QStringLiteral("bob"), QStringLiteral("alice"), &error), qPrintable(error));
		QCOMPARE(store.unreadDirectMessageCount(QStringLiteral("bob"), QStringLiteral("alice"), &error), 0);
		QVERIFY2(store.sendDirectMessage(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("Another message"), nullptr, &error), qPrintable(error));
		QCOMPARE(store.unreadDirectMessageCount(QStringLiteral("bob"), QStringLiteral("alice"), &error), 1);
		qint64 hiddenMessageId = 0;
		QVERIFY2(store.sendDirectMessage(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("Delete only for Bob"), &hiddenMessageId, &error), qPrintable(error));
		QVERIFY2(store.deleteDirectMessage(QStringLiteral("bob"), hiddenMessageId, &error), qPrintable(error));
		QCOMPARE(store.directMessageHistory(QStringLiteral("bob"), QStringLiteral("alice"), 0, 10, &error).size(), 2);
		QCOMPARE(store.directMessageHistory(QStringLiteral("alice"), QStringLiteral("bob"), 0, 10, &error).size(), 3);
		QVERIFY2(store.deleteInboxConversation(QStringLiteral("bob"), QStringLiteral("alice"), &error), qPrintable(error));
		QVERIFY(store.inboxContacts(QStringLiteral("bob"), &error).isEmpty());
		QVERIFY(!store.inboxContacts(QStringLiteral("alice"), &error).isEmpty());
		QVERIFY2(store.sendDirectMessage(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("Conversation returns with new message"), nullptr, &error), qPrintable(error));
		QCOMPARE(store.inboxContacts(QStringLiteral("bob"), &error).size(), 1);

		QVERIFY2(store.setBlocked(QStringLiteral("bob"), QStringLiteral("alice"), true, &error), qPrintable(error));
		QVERIFY(!store.areFriends(QStringLiteral("alice"), QStringLiteral("bob"), &error));
		QVERIFY(!store.sendDirectMessage(QStringLiteral("alice"), QStringLiteral("bob"),
			QStringLiteral("Blocked"), nullptr, &error));
		error.clear();
		QVERIFY(!store.sendFriendRequest(QStringLiteral("alice"), QStringLiteral("bob"), nullptr, &error));
		error.clear();
		QVERIFY2(store.setBlocked(QStringLiteral("bob"), QStringLiteral("alice"), false, &error), qPrintable(error));
		QVERIFY2(store.sendFriendRequest(QStringLiteral("alice"), QStringLiteral("bob"), &requestId, &error), qPrintable(error));
	}

	void persistsAccountsAndScores()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString databasePath = directory.filePath(QStringLiteral("accounts.sqlite"));
		QString error;
		{
			gtd::AccountStore store(databasePath);
			QVERIFY2(store.initialize(&error), qPrintable(error));
			QVERIFY2(store.createAccount(QStringLiteral("Sketcher"), QStringLiteral("correct horse"),
										 QStringLiteral("Sketch Player"), &error), qPrintable(error));
			const auto profile = store.authenticate(QStringLiteral("sketcher"), QStringLiteral("correct horse"), &error);
			QVERIFY2(profile.has_value(), qPrintable(error));
			QCOMPARE(profile->displayName, QStringLiteral("Sketch Player"));
			QVERIFY(!store.authenticate(QStringLiteral("sketcher"), QStringLiteral("wrong password"), &error));
			QVERIFY2(store.addScore(QStringLiteral("sketcher"), 7, false, &error), qPrintable(error));
			QVERIFY2(store.recordRoundResult(QStringLiteral("sketcher"), true, &error), qPrintable(error));
			QVERIFY2(store.recordRoundResult(QStringLiteral("sketcher"), false, &error), qPrintable(error));
			QVERIFY2(store.recordRoundAbandonment(QStringLiteral("sketcher"), &error), qPrintable(error));
		}

		gtd::AccountStore reopened(databasePath);
		QVERIFY2(reopened.initialize(&error), qPrintable(error));
		const auto profile = reopened.profile(QStringLiteral("SKETCHER"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->totalScore, 7);
		QCOMPARE(profile->roundsWon, 1);
		QCOMPARE(profile->roundsLost, 1);
		QCOMPARE(profile->roundsAbandoned, 1);
		QCOMPARE(profile->guessingScore, 7);
		QCOMPARE(profile->drawingScore, 0);
		QCOMPARE(profile->guessingRank, QStringLiteral("Word Seeker I"));
		QCOMPARE(profile->drawingRank, QStringLiteral("Pencil I"));
		QCOMPARE(profile->leaderboardRank, 1);
	}

	void assignsScoreTiersAndLeaderboardRanks()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		QVERIFY2(store.createAccount(QStringLiteral("first"), QStringLiteral("password1"),
			QStringLiteral("First"), &error), qPrintable(error));
		QVERIFY2(store.createAccount(QStringLiteral("second"), QStringLiteral("password2"),
			QStringLiteral("Second"), &error), qPrintable(error));
		const auto freshFirst = store.profile(QStringLiteral("first"), &error);
		const auto freshSecond = store.profile(QStringLiteral("second"), &error);
		QVERIFY2(freshFirst.has_value(), qPrintable(error));
		QVERIFY2(freshSecond.has_value(), qPrintable(error));
		QCOMPARE(freshFirst->leaderboardRank, 0);
		QCOMPARE(freshSecond->leaderboardRank, 0);
		QVERIFY2(store.addScore(QStringLiteral("first"), 1000, false, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("second"), 2000, true, &error), qPrintable(error));
		const auto first = store.profile(QStringLiteral("first"), &error);
		QVERIFY2(first.has_value(), qPrintable(error));
		QCOMPARE(first->guessingScore, 1000);
		QCOMPARE(first->drawingScore, 0);
		QCOMPARE(first->totalScore, 1000);
		QCOMPARE(first->guessingRank, QStringLiteral("Clue Finder II"));
		QCOMPARE(first->drawingRank, QStringLiteral("Pencil I"));
		QCOMPARE(first->leaderboardRank, 2);
		const auto second = store.profile(QStringLiteral("second"), &error);
		QVERIFY2(second.has_value(), qPrintable(error));
		QCOMPARE(second->guessingScore, 0);
		QCOMPARE(second->drawingScore, 2000);
		QCOMPARE(second->guessingRank, QStringLiteral("Word Seeker I"));
		QCOMPARE(second->drawingRank, QStringLiteral("Doodler I"));
		QCOMPARE(second->totalScore, 2000);
		QCOMPARE(second->leaderboardRank, 1);
	}

	void ranksEqualScoresByFirstAchievement()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		QVERIFY2(store.createAccount(QStringLiteral("first"), QStringLiteral("password1"),
			QStringLiteral("First"), &error), qPrintable(error));
		QVERIFY2(store.createAccount(QStringLiteral("second"), QStringLiteral("password2"),
			QStringLiteral("Second"), &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("first"), 100, false, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("second"), 50, true, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("second"), 0, false, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("second"), 50, false, &error), qPrintable(error));

		const auto first = store.profile(QStringLiteral("first"), &error);
		const auto second = store.profile(QStringLiteral("second"), &error);
		QVERIFY2(first.has_value(), qPrintable(error));
		QVERIFY2(second.has_value(), qPrintable(error));
		QCOMPARE(first->totalScore, 100);
		QCOMPARE(second->totalScore, 100);
		QCOMPARE(first->leaderboardRank, 1);
		QCOMPARE(second->leaderboardRank, 2);
	}

	void persistsCountryAndListsIndependentLeaderboards()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		for (const QString& username : {QStringLiteral("alpha"), QStringLiteral("bravo"), QStringLiteral("charlie")})
			QVERIFY2(store.createAccount(username, QStringLiteral("password1"), username, &error), qPrintable(error));
		QVERIFY2(store.updateCountry(QStringLiteral("alpha"), QStringLiteral("us"), &error), qPrintable(error));
		for (const QString& countryCode : {QStringLiteral("XK"), QStringLiteral("GB-ENG"),
			QStringLiteral("GB-NIR"), QStringLiteral("GB-SCT"), QStringLiteral("GB-WLS")})
			QVERIFY2(store.updateCountry(QStringLiteral("alpha"), countryCode, &error), qPrintable(error));
		QVERIFY2(store.updateCountry(QStringLiteral("alpha"), QStringLiteral("US"), &error), qPrintable(error));
		QVERIFY(!store.updateCountry(QStringLiteral("bravo"), QStringLiteral("ZZ"), &error));
		QVERIFY2(store.addScore(QStringLiteral("alpha"), 100, false, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("bravo"), 200, false, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("charlie"), 300, true, &error), qPrintable(error));
		QVERIFY2(store.addScore(QStringLiteral("alpha"), 400, true, &error), qPrintable(error));

		const auto alpha = store.profile(QStringLiteral("alpha"), &error);
		QVERIFY2(alpha.has_value(), qPrintable(error));
		QCOMPARE(alpha->countryCode, QStringLiteral("US"));
		QCOMPARE(alpha->guessingLeaderboardRank, 2);
		QCOMPARE(alpha->drawingLeaderboardRank, 1);
		const QList<gtd::PlayerProfile> guessing = store.topPlayers(false, 10000, &error);
		QCOMPARE(guessing.size(), 2);
		QCOMPARE(guessing.at(0).username, QStringLiteral("bravo"));
		QCOMPARE(guessing.at(1).username, QStringLiteral("alpha"));
		const QList<gtd::PlayerProfile> drawing = store.topPlayers(true, 1000, &error);
		QCOMPARE(drawing.size(), 2);
		QCOMPARE(drawing.at(0).username, QStringLiteral("alpha"));
		QCOMPARE(drawing.at(0).countryCode, QStringLiteral("US"));
		QCOMPARE(drawing.at(1).username, QStringLiteral("charlie"));
	}

	void updatesProfileAndPasswordSecurely()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		QVERIFY2(store.createAccount(QStringLiteral("profileuser"), QStringLiteral("oldpassword"),
			QStringLiteral("Old Nickname"), &error), qPrintable(error));
		QVERIFY2(store.updateProfile(QStringLiteral("profileuser"), QStringLiteral("New Nickname"),
			QStringLiteral("ca"), QString(), QString(), &error), qPrintable(error));
		for (const QString& countryCode : {QStringLiteral("XK"), QStringLiteral("GB-ENG"),
			QStringLiteral("GB-NIR"), QStringLiteral("GB-SCT"), QStringLiteral("GB-WLS")})
			QVERIFY2(store.updateProfile(QStringLiteral("profileuser"), QStringLiteral("New Nickname"),
				countryCode, QString(), QString(), &error), qPrintable(error));
		QVERIFY(!store.updateProfile(QStringLiteral("profileuser"), QStringLiteral("Changed"),
			QStringLiteral("US"), QStringLiteral("wrongpassword"), QStringLiteral("newpassword"), &error));
		QVERIFY(!store.updateProfile(QStringLiteral("profileuser"), QStringLiteral("Changed"),
			QStringLiteral("US"), QStringLiteral("oldpassword"), QStringLiteral("short"), &error));
		QVERIFY2(store.updateProfile(QStringLiteral("profileuser"), QStringLiteral("Final Nickname"),
			QStringLiteral("gb"), QStringLiteral("oldpassword"), QStringLiteral("newpassword"), &error), qPrintable(error));
		QVERIFY(!store.authenticate(QStringLiteral("profileuser"), QStringLiteral("oldpassword"), &error));
		const auto authenticated = store.authenticate(QStringLiteral("profileuser"), QStringLiteral("newpassword"), &error);
		QVERIFY2(authenticated.has_value(), qPrintable(error));
		QCOMPARE(authenticated->displayName, QStringLiteral("Final Nickname"));
		QCOMPARE(authenticated->countryCode, QStringLiteral("GB"));
	}

	void rejectsInvalidAccounts()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		QVERIFY(!store.createAccount(QStringLiteral("ab"), QStringLiteral("password"),
									 QStringLiteral("Player"), &error));
		QVERIFY(!store.createAccount(QStringLiteral("player"), QStringLiteral("short"),
									 QStringLiteral("Player"), &error));
	}

	void validatesTextFieldsAndKeepsSqlLikeTextAsData()
	{
		QTemporaryDir directory;
		gtd::AccountStore store(directory.filePath(QStringLiteral("accounts.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		const QString sqlLikeName = QStringLiteral("' OR 1=1 --");
		QVERIFY2(store.createAccount(QStringLiteral("safeplayer"), QStringLiteral("password123"),
			sqlLikeName, &error), qPrintable(error));
		const auto profile = store.profile(QStringLiteral("safeplayer"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->displayName, sqlLikeName);

		QVERIFY(!store.createAccount(QStringLiteral("longdisplay"), QStringLiteral("password123"),
			QString(33, QLatin1Char('x')), &error));
		QVERIFY(!store.createAccount(QStringLiteral("controlname"), QStringLiteral("password123"),
			QStringLiteral("First\nSecond"), &error));
		QVERIFY(!store.updateProfile(QStringLiteral("safeplayer"), QStringLiteral("First\nSecond"),
			QString(), QString(), QString(), &error));
		QVERIFY(!store.authenticate(QStringLiteral("safeplayer"), QString(129, QLatin1Char('x')), &error));
	}

	void persistsServerRolesAndSuperAdmins()
	{
		QTemporaryDir directory;
		const QString path = directory.filePath(QStringLiteral("moderation.sqlite"));
		QString error;
		gtd::AccountStore store(path);
		QVERIFY2(store.initialize(&error), qPrintable(error));
		for (const QString& user : {QStringLiteral("owner"), QStringLiteral("helper"), QStringLiteral("player")})
			QVERIFY2(store.createAccount(user, QStringLiteral("password123"), user, &error), qPrintable(error));

		QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("superadmin-seed"));
		seed.setDatabaseName(path);
		QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
		QSqlQuery seedQuery(seed);
		seedQuery.prepare(QStringLiteral("INSERT INTO master_super_admins(username) VALUES(:username)"));
		seedQuery.bindValue(QStringLiteral(":username"), QStringLiteral("owner"));
		QVERIFY2(seedQuery.exec(), qPrintable(seedQuery.lastError().text()));
		seed.close();
		seed = QSqlDatabase();
		QSqlDatabase::removeDatabase(QStringLiteral("superadmin-seed"));

		QVERIFY(store.isMasterSuperAdmin(QStringLiteral("OWNER"), &error));
		QVERIFY(!store.isMasterSuperAdmin(QStringLiteral("helper"), &error));		
		QVERIFY2(store.setServerRole(QStringLiteral("room-server-a"), QStringLiteral("helper"),
			QStringLiteral("moderator"), QStringLiteral("owner"), &error), qPrintable(error));
		QVERIFY2(store.setServerRole(QStringLiteral("room-server-a"), QStringLiteral("player"),
			QStringLiteral("admin"), QStringLiteral("owner"), &error), qPrintable(error));
		QCOMPARE(store.serverRole(QStringLiteral("room-server-a"), QStringLiteral("PLAYER"), &error),
			QStringLiteral("admin"));
		QCOMPARE(store.serverRole(QStringLiteral("room-server-b"), QStringLiteral("player"), &error),
			QStringLiteral("player"));
		QCOMPARE(store.serverStaff(QStringLiteral("room-server-a"), &error).size(), 2);
		QVERIFY2(store.setServerRole(QStringLiteral("room-server-a"), QStringLiteral("helper"),
			QStringLiteral("player"), QStringLiteral("owner"), &error), qPrintable(error));
		QCOMPARE(store.serverRole(QStringLiteral("room-server-a"), QStringLiteral("helper"), &error),
			QStringLiteral("player"));
	}

	void persistsScopedSanctionsAndViewerMutes()
	{
		QTemporaryDir directory;
		const QString path = directory.filePath(QStringLiteral("sanctions.sqlite"));
		QString error;
		{
			gtd::AccountStore store(path);
			QVERIFY2(store.initialize(&error), qPrintable(error));
			for (const QString& user : {QStringLiteral("moderator"), QStringLiteral("target"), QStringLiteral("viewer")})
				QVERIFY2(store.createAccount(user, QStringLiteral("password123"), user, &error), qPrintable(error));

			gtd::ServerSanction ban;
			ban.serverId = QStringLiteral("room-server-a");
			ban.targetUsername = QStringLiteral("target");
			ban.kind = QStringLiteral("ban");
			ban.reason = QStringLiteral("Repeated disruption");
			ban.issuedBy = QStringLiteral("moderator");
			qint64 banId = 0;
			QVERIFY2(store.addServerSanction(ban, &banId, &error), qPrintable(error));
			QVERIFY(banId > 0);

			gtd::ServerSanction mute = ban;
			mute.kind = QStringLiteral("mute");
			mute.textChat = true;
			mute.voiceChat = false;
			mute.reason = QStringLiteral("Text flood");
			mute.expiresAt = QDateTime::currentSecsSinceEpoch() + 60;
			QVERIFY2(store.addServerSanction(mute, nullptr, &error), qPrintable(error));
			QCOMPARE(store.activeServerSanctions(QStringLiteral("room-server-a"),
				QStringLiteral("target"), QDateTime::currentSecsSinceEpoch(), &error).size(), 2);
			QCOMPARE(store.activeServerSanctions(QStringLiteral("room-server-b"), {}, -1, &error).size(), 0);
			QCOMPARE(store.activeServerSanctions(QStringLiteral("room-server-a"),
				QStringLiteral("target"), mute.expiresAt, &error).size(), 1);
			QVERIFY2(store.revokeServerSanction(QStringLiteral("room-server-a"), banId,
				QStringLiteral("moderator"), &error), qPrintable(error));
			QCOMPARE(store.activeServerSanctions(QStringLiteral("room-server-a"),
				QStringLiteral("target"), QDateTime::currentSecsSinceEpoch(), &error).size(), 1);

			QVERIFY2(store.setPlayerMute(QStringLiteral("room-server-a"), QStringLiteral("viewer"),
				QStringLiteral("target"), false, true, &error), qPrintable(error));
			const auto preferences = store.playerMutePreferences(QStringLiteral("room-server-a"),
				QStringLiteral("viewer"), &error);
			QCOMPARE(preferences.size(), 1);
			QVERIFY(!preferences.first().textChat);
			QVERIFY(preferences.first().voiceChat);
			QCOMPARE(store.playerMutePreferences(QStringLiteral("room-server-b"),
				QStringLiteral("viewer"), &error).size(), 0);
		}

		gtd::AccountStore reopened(path);
		QVERIFY2(reopened.initialize(&error), qPrintable(error));
		const auto preferences = reopened.playerMutePreferences(QStringLiteral("room-server-a"),
			QStringLiteral("viewer"), &error);
		QCOMPARE(preferences.size(), 1);
		QVERIFY2(reopened.setPlayerMute(QStringLiteral("room-server-a"), QStringLiteral("viewer"),
			QStringLiteral("target"), false, false, &error), qPrintable(error));
		QCOMPARE(reopened.playerMutePreferences(QStringLiteral("room-server-a"),
			QStringLiteral("viewer"), &error).size(), 0);
	}

	void countsIndependentPlayerReports()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		gtd::AccountStore store(directory.filePath(QStringLiteral("reports.sqlite")));
		QString error;
		QVERIFY2(store.initialize(&error), qPrintable(error));
		for (const QString& username : {QStringLiteral("reporter1"), QStringLiteral("reporter2"),
			QStringLiteral("reporter3"), QStringLiteral("reported")})
			QVERIFY2(store.createAccount(username, QStringLiteral("password123"), username, &error), qPrintable(error));

		int recentCount = 0;
		QVERIFY2(store.reportPlayer(QStringLiteral("reports-server"), QStringLiteral("reporter1"),
			QStringLiteral("reported"), QStringLiteral("Harassment"), &recentCount, &error), qPrintable(error));
		QCOMPARE(recentCount, 1);
		QVERIFY2(store.reportPlayer(QStringLiteral("reports-server"), QStringLiteral("reporter1"),
			QStringLiteral("reported"), QStringLiteral("Updated reason"), &recentCount, &error), qPrintable(error));
		QCOMPARE(recentCount, 1);
		QVERIFY2(store.reportPlayer(QStringLiteral("reports-server"), QStringLiteral("reporter2"),
			QStringLiteral("reported"), QStringLiteral("Harassment"), &recentCount, &error), qPrintable(error));
		QCOMPARE(recentCount, 2);
		QVERIFY(!store.reportPlayer(QStringLiteral("reports-server"), QStringLiteral("reporter1"),
			QStringLiteral("reporter1"), QStringLiteral("Self report"), &recentCount, &error));
	}
};

QTEST_GUILESS_MAIN(AccountStoreTests)
#include "test_account_store.moc"
