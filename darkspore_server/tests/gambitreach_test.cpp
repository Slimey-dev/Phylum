#include <doctest/doctest.h>

#include "game/gambitreach.h"

using Game::GambitReach;

TEST_CASE("a gambit in reach is cast, whether or not its ability pursues") {
	CHECK(GambitReach::Decide(true, false, true) == GambitReach::Answer::Cast);
	CHECK(GambitReach::Decide(true, true, true) == GambitReach::Answer::Cast);
	// A turret casts at what is in reach.
	CHECK(GambitReach::Decide(true, false, false) == GambitReach::Answer::Cast);
}

TEST_CASE("out of reach, only an ability with shouldPursue walks up for its cast") {
	// A melee swing (the melee template sets shouldPursue) chases its target.
	CHECK(GambitReach::Decide(false, true, true) == GambitReach::Answer::Pursue);
	// A shot (the projectile template does not) is refused: a Zelem shooter blinks in instead.
	CHECK(GambitReach::Decide(false, false, true) == GambitReach::Answer::TooFar);
}

TEST_CASE("an agent that cannot walk pursues nothing") {
	CHECK(GambitReach::Decide(false, true, false) == GambitReach::Answer::TooFar);
	CHECK(GambitReach::Decide(false, false, false) == GambitReach::Answer::TooFar);
}
