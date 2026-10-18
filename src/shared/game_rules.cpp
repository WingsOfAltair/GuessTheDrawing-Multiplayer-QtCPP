#include "shared/game_rules.h"

#include <QtGlobal>

namespace gtd {

int pointsForCorrectGuessPlacement(int placement)
{
	switch (placement) {
	case 1: return 10;
	case 2: return 7;
	case 3: return 5;
	default: return placement > 3 ? 2 : 0;
	}
}

int pointsForDrawerPerCorrectGuess()
{
	return 2;
}

int clampRoundSeconds(int seconds)
{
	return qBound(10, seconds, 600);
}

}
