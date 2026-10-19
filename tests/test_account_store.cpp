#include "storage/account_store.h"

#include <QTemporaryDir>
#include <QtTest>

class AccountStoreTests final : public QObject {
	Q_OBJECT

private slots:
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
		}

		gtd::AccountStore reopened(databasePath);
		QVERIFY2(reopened.initialize(&error), qPrintable(error));
		const auto profile = reopened.profile(QStringLiteral("SKETCHER"), &error);
		QVERIFY2(profile.has_value(), qPrintable(error));
		QCOMPARE(profile->totalScore, 7);
		QCOMPARE(profile->roundsWon, 1);
		QCOMPARE(profile->roundsLost, 1);
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
};

QTEST_GUILESS_MAIN(AccountStoreTests)
#include "test_account_store.moc"
