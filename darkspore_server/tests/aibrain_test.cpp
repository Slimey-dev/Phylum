
#include <doctest/doctest.h>

#include "game/aibrain.h"
#include "game/aitargeting.h"

using Game::AIStimuli;
using Game::BrainNode;
using Game::BrainProfile;
using Game::BrainState;
using Game::ChooseNode;
namespace AIStimulus = Game::AIStimulus;

namespace {
	// ZelemBasicHybrid's AI definition (aidefinition/ZelemBasicHybrid.AIDefinition.xml).
	constexpr uint32_t firstAggroAnim = 0x7f26db9f;
	constexpr uint32_t firstAggroFaceTarget = 0xf420b7a4;

	BrainProfile Hybrid() {
		BrainProfile profile;
		profile.preAggroIdle = "nBehavior_Invisible";
		profile.secondaryPreAggroIdle = "nBehavior_Wander";
		profile.combatIdle = "nBehavior_Wander";
		profile.passiveIdle = "nBehavior_Idle";
		profile.firstAggroAbility = firstAggroAnim;
		profile.secondaryFirstAggroAbility = firstAggroFaceTarget;
		profile.firstAlertAbility = firstAggroAnim;
		profile.useSecondaryStart = 0.8f;
		return profile;
	}

	// A mob in a fight it has aggroed into, with nothing pending.
	BrainState Fighting() {
		BrainState state;
		state.hasAggroed = true;
		state.hasTarget = true;
		state.hasIdle = true;
		return state;
	}
}

TEST_CASE("a stimulus lasts its time and then goes") {
	AIStimuli stimuli;
	stimuli.Post(AIStimulus::FirstAggro, 10.f);
	CHECK(stimuli.IsActive(AIStimulus::FirstAggro));
	CHECK_FALSE(stimuli.IsActive(AIStimulus::FirstAlert));

	stimuli.Decay(9.75f);
	CHECK(stimuli.IsActive(AIStimulus::FirstAggro));
	stimuli.Decay(0.25f);
	CHECK_FALSE(stimuli.IsActive(AIStimulus::FirstAggro));
	CHECK(stimuli.Active() == 0);
}

TEST_CASE("posting a stimulus again restarts its time rather than taking a second slot") {
	AIStimuli stimuli;
	stimuli.Post(AIStimulus::FirstAlert, 1.f);
	stimuli.Decay(0.5f);
	stimuli.Post(AIStimulus::FirstAlert, 1.f);
	stimuli.Decay(0.75f);
	CHECK(stimuli.IsActive(AIStimulus::FirstAlert));

	stimuli.Clear(AIStimulus::FirstAlert);
	CHECK_FALSE(stimuli.IsActive(AIStimulus::FirstAlert));
}

TEST_CASE("with every slot taken, a new stimulus replaces the one with the least time left") {
	AIStimuli stimuli;
	for (uint32_t bit = 0; bit < AIStimuli::slotCount; ++bit) {
		stimuli.Post(1u << bit, 10.f + static_cast<float>(bit));
	}
	stimuli.Post(AIStimulus::FirstAlert, 10.f);
	CHECK(stimuli.IsActive(AIStimulus::FirstAlert));
	// Bit 0 had the least time left.
	CHECK_FALSE(stimuli.IsActive(1u << 0));
	CHECK(stimuli.IsActive(1u << 1));
}

TEST_CASE("IgnoreFirstAggro's mask clears every aggro reaction and nothing else") {
	AIStimuli stimuli;
	stimuli.Post(AIStimulus::FirstAggro, 10.f);
	stimuli.Post(AIStimulus::SubsequentAggro, 10.f);
	stimuli.Post(AIStimulus::FirstAlert, 10.f);
	stimuli.Post(AIStimulus::TookDamage, 10.f);
	stimuli.Clear(AIStimulus::AggroReactions);
	CHECK(stimuli.Active() == AIStimulus::TookDamage);
}

TEST_CASE("death comes first, then a running cast, which holds the agent through anything else") {
	auto state = Fighting();
	state.stimuli = AIStimulus::FirstAggro;
	state.castRunning = true;
	CHECK(ChooseNode(state) == BrainNode::HoldCast);

	state.dead = true;
	CHECK(ChooseNode(state) == BrainNode::Death);
}

TEST_CASE("a first aggro reacts before the fight, and needs a target to face") {
	auto state = Fighting();
	state.stimuli = AIStimulus::FirstAggro;
	CHECK(ChooseNode(state) == BrainNode::FirstAggroCast);

	state.hasTarget = false;
	CHECK(ChooseNode(state) == BrainNode::Idle);
}

TEST_CASE("an alerted agent stays in its idle until its delay is over, then reacts") {
	BrainState state;
	state.hasTarget = true;
	state.hasIdle = true;
	state.stimuli = AIStimulus::FirstAlert;
	state.hasAggroed = false;
	CHECK(ChooseNode(state) == BrainNode::Idle);

	state.hasAggroed = true;
	CHECK(ChooseNode(state) == BrainNode::FirstAlertCast);
}

TEST_CASE("a new fight after the last one ended reacts with the subsequent aggro") {
	auto state = Fighting();
	state.stimuli = AIStimulus::SubsequentAggro;
	CHECK(ChooseNode(state) == BrainNode::SubsequentAggroCast);
}

TEST_CASE("with a target and aggroed it fights, otherwise it idles or wanders") {
	CHECK(ChooseNode(Fighting()) == BrainNode::Combat);

	BrainState calm;
	calm.hasIdle = true;
	CHECK(ChooseNode(calm) == BrainNode::Idle);
	calm.hasIdle = false;
	CHECK(ChooseNode(calm) == BrainNode::Wander);

	// A buffer that runs its gambits before it has a target.
	calm.alwaysRun = true;
	CHECK(ChooseNode(calm) == BrainNode::Combat);
}

TEST_CASE("anything but a creature has no idle to run") {
	// An obelisk or a fire wall: its passive does the work.
	BrainState obelisk;
	obelisk.creature = false;
	CHECK(ChooseNode(obelisk) == BrainNode::Rest);
	obelisk.hasIdle = true;
	CHECK(ChooseNode(obelisk) == BrainNode::Rest);

	// It still holds a running cast and fights what it lists.
	auto turret = Fighting();
	turret.creature = false;
	CHECK(ChooseNode(turret) == BrainNode::Combat);
}

TEST_CASE("a secondary start is a roll below useSecondaryStart") {
	CHECK(Game::RollSecondaryStart(0.8f, 0.f));
	CHECK(Game::RollSecondaryStart(0.8f, 0.79f));
	CHECK_FALSE(Game::RollSecondaryStart(0.8f, 0.8f));
	CHECK_FALSE(Game::RollSecondaryStart(0.f, 0.f));
}

TEST_CASE("the idle is the pre-aggro one until the first aggro, and the passive one after") {
	const auto hybrid = Hybrid();
	CHECK(Game::IdleFor(hybrid, false, false) == "nBehavior_Invisible");
	CHECK(Game::IdleFor(hybrid, true, false) == "nBehavior_Wander");
	CHECK(Game::IdleFor(hybrid, false, true) == "nBehavior_Idle");
	CHECK(Game::IdleFor(hybrid, true, true) == "nBehavior_Idle");

	// NomadSnipe has no passiveIdle: after its fight it wanders.
	auto snipe = hybrid;
	snipe.passiveIdle.clear();
	CHECK(Game::IdleFor(snipe, false, true).empty());

	// A secondary start whose preAggroIdle2 is empty goes straight to the passive idle (0x9e3d00).
	auto ranged = hybrid;
	ranged.secondaryPreAggroIdle.clear();
	CHECK(Game::IdleFor(ranged, true, false) == "nBehavior_Idle");
}

TEST_CASE("the combat idle is the definition's, else nBehavior_CombatIdle") {
	CHECK(Game::CombatIdleFor(Hybrid()) == "nBehavior_Wander");
	BrainProfile ranged;
	CHECK(Game::CombatIdleFor(ranged) == Game::combatIdleBehavior);
}

TEST_CASE("the first aggro casts firstAggroAbility, or firstAggroAbility2 on a secondary start") {
	const auto hybrid = Hybrid();
	CHECK(Game::FirstAggroAbilityFor(hybrid, false, 0) == firstAggroAnim);
	CHECK(Game::FirstAggroAbilityFor(hybrid, true, 0) == firstAggroFaceTarget);
	CHECK(Game::FirstAlertAbilityFor(hybrid, 0) == firstAggroAnim);

	// OverrideFirstAggro wins for both.
	constexpr uint32_t shadowTeleportExit = 0x1234;
	CHECK(Game::FirstAggroAbilityFor(hybrid, true, shadowTeleportExit) == shadowTeleportExit);
	CHECK(Game::FirstAlertAbilityFor(hybrid, shadowTeleportExit) == shadowTeleportExit);
}

TEST_CASE("an NPC perceives out to the larger of its aggro and alert ranges, 20 without a class") {
	// ZelemBasicRepair: aggro 14, alert 17.
	CHECK(Game::PerceptionRadius(true, 14.f, 17.f) == 17.f);
	CHECK(Game::PerceptionRadius(true, 22.f, 16.f) == 22.f);
	CHECK(Game::PerceptionRadius(false, 0.f, 0.f) == 20.f);
}

TEST_CASE("a point touches the circle only strictly inside, counting its radius") {
	const glm::vec3 center(0, 0, 0);
	CHECK(Game::TouchesCircle(center, 10.f, { 9.9f, 0, 0 }, 0.f));
	CHECK_FALSE(Game::TouchesCircle(center, 10.f, { 10.f, 0, 0 }, 0.f));
	CHECK(Game::TouchesCircle(center, 10.f, { 10.5f, 0, 0 }, 1.f));
	CHECK_FALSE(Game::TouchesCircle(center, 10.f, { 11.f, 0, 0 }, 1.f));
}

TEST_CASE("the aggro trigger takes live, tangible heroes and summons of another team") {
	constexpr uint8_t mobTeam = 0;
	Game::AggroEntrant hero;
	hero.team = 1;
	CHECK(Game::IsAggroEntrant(mobTeam, hero));

	auto ally = hero;
	ally.team = mobTeam;
	CHECK_FALSE(Game::IsAggroEntrant(mobTeam, ally));

	auto dead = hero;
	dead.alive = false;
	CHECK_FALSE(Game::IsAggroEntrant(mobTeam, dead));

	auto phased = hero;
	phased.intangible = true;
	CHECK_FALSE(Game::IsAggroEntrant(mobTeam, phased));

	auto prop = hero;
	prop.combatant = false;
	CHECK_FALSE(Game::IsAggroEntrant(mobTeam, prop));

	// The level's friendly creatures share the heroes' team but start no fight.
	auto friendly = hero;
	friendly.heroSide = false;
	CHECK_FALSE(Game::IsAggroEntrant(mobTeam, friendly));
}
