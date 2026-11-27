
// Include
#include "gambitreach.h"

// Game
namespace Game {
	GambitReach::Answer GambitReach::Decide(bool inReach, bool shouldPursue, bool canWalk) {
		if (inReach) {
			return Answer::Cast;
		}
		return shouldPursue && canWalk ? Answer::Pursue : Answer::TooFar;
	}
}
