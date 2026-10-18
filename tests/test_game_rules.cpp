#include "shared/game_rules.h"

#include <QtTest>

class GameRuleTests final : public QObject {
	Q_OBJECT

private slots:
	void awardsDescendingPlacementPoints()
	{
		QCOMPARE(gtd::pointsForCorrectGuessPlacement(1), 10);
		QCOMPARE(gtd::pointsForCorrectGuessPlacement(2), 7);
		QCOMPARE(gtd::pointsForCorrectGuessPlacement(3), 5);
		QCOMPARE(gtd::pointsForCorrectGuessPlacement(4), 2);
		QCOMPARE(gtd::pointsForCorrectGuessPlacement(0), 0);
	}

	void clampsRoundDuration()
	{
		QCOMPARE(gtd::clampRoundSeconds(0), 10);
		QCOMPARE(gtd::clampRoundSeconds(60), 60);
		QCOMPARE(gtd::clampRoundSeconds(1000), 600);
		QCOMPARE(gtd::pointsForDrawerPerCorrectGuess(), 2);
	}
};

QTEST_GUILESS_MAIN(GameRuleTests)
#include "test_game_rules.moc"
