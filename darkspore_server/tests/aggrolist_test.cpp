
#include <doctest/doctest.h>

#include "game/aggrolist.h"

#include <map>

using Game::AggroCandidate;
using Game::AggroList;
using Game::AggroStimulus;
namespace AggroFlags = Game::AggroFlags;

namespace {
	constexpr uint64_t now = 100000;
	constexpr uint32_t hero = 1;
	constexpr uint32_t pet = 2;
	constexpr uint32_t other = 3;

	// An add with no attribute scaling, as most sources have.
	AggroStimulus Add(AggroList& list, uint32_t id, float amount, uint32_t flags = AggroFlags::Script, float roll = 0.f) {
		return list.Add(id, amount, flags, 0.f, 0.f, now, roll);
	}

	// Every id resolves to a creature that may be fought, unless the map says otherwise.
	auto Lookup(const std::map<uint32_t, std::optional<AggroCandidate>>& special = {}) {
		return [special](uint32_t id) -> std::optional<AggroCandidate> {
			if (const auto it = special.find(id); it != special.end()) {
				return it->second;
			}
			return AggroCandidate {};
		};
	}
}

TEST_CASE("an add is scaled by the source's aggro attributes and is never below 0.01") {
	AggroList list;
	list.Add(hero, 10.f, AggroFlags::Script, 0.5f, 0.2f, now, 0.f);
	// 10 * (1 + 0.5) * (1 - 0.2)
	CHECK(list.Get(hero) == doctest::Approx(12.f));

	// taunt_end takes its 100 back with -100, which adds the least an add can.
	Add(list, pet, 100.f);
	Add(list, pet, -100.f);
	CHECK(list.Get(pet) == doctest::Approx(100.01f));

	Add(list, other, 0.f);
	CHECK(list.Get(other) == doctest::Approx(AggroList::minimumThreat));
}

TEST_CASE("an add to a listed source adds to its entry, a new one is listed") {
	AggroList list;
	Add(list, hero, 5.f);
	Add(list, hero, 3.f);
	Add(list, pet, 1.f);
	CHECK(list.Get(hero) == doctest::Approx(8.f));
	CHECK(list.GetIds() == std::vector<uint32_t> { hero, pet });
	CHECK(list.Get(other) == 0.f);
	CHECK_FALSE(list.Contains(other));
}

TEST_CASE("the list is highest threat first, and a tie keeps the order the entries came in") {
	AggroList list;
	Add(list, hero, 5.f);
	Add(list, pet, 5.f);
	Add(list, other, 5.f);
	CHECK(list.GetIds() == std::vector<uint32_t> { hero, pet, other });

	// The last one listed overtakes the others once it has more.
	Add(list, other, 1.f);
	CHECK(list.GetIds() == std::vector<uint32_t> { other, hero, pet });

	// A tie goes to the one listed first, whichever reached that threat first.
	Add(list, pet, 1.f);
	CHECK(list.GetIds() == std::vector<uint32_t> { pet, other, hero });
	Add(list, hero, 1.f);
	CHECK(list.GetIds() == std::vector<uint32_t> { hero, pet, other });
}

TEST_CASE("Entangle's threat puts its caster first") {
	AggroList list;
	Add(list, hero, 20.f);
	Add(list, pet, 4.f);
	// ability_entangle.lua: the target's threat minus the caster's, plus 5.
	Add(list, pet, list.Get(hero) - list.Get(pet) + 5.f);
	CHECK(list.GetIds().front() == pet);
	CHECK(list.Get(pet) == doctest::Approx(25.f));
}

TEST_CASE("the first entry a trigger volume or damage lists is a first aggro") {
	AggroList perceived;
	CHECK(Add(perceived, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::FirstAggro);
	CHECK(perceived.HasAggroed(now));
	// Only the first entry ever reacts.
	CHECK(Add(perceived, pet, 5.f, AggroFlags::Perceived) == AggroStimulus::None);
	CHECK(Add(perceived, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::None);

	AggroList damaged;
	CHECK(Add(damaged, hero, 12.f, AggroFlags::Damage) == AggroStimulus::FirstAggro);
}

TEST_CASE("a script's first entry reacts with nothing, but the agent has aggroed") {
	AggroList list;
	CHECK(Add(list, hero, 100.f, AggroFlags::Script) == AggroStimulus::None);
	CHECK(list.IsLatched());
	CHECK(list.HasAggroed(now));
	CHECK(Add(list, pet, 5.f, AggroFlags::Perceived) == AggroStimulus::None);
}

TEST_CASE("an alerted first entry reacts 0.5 to 1.25 seconds late") {
	AggroList early;
	CHECK(Add(early, hero, 5.f, AggroFlags::Alerted, 0.f) == AggroStimulus::FirstAlert);
	CHECK(early.IsLatched());
	CHECK_FALSE(early.HasAggroed(now + 499));
	CHECK(early.HasAggroed(now + 500));

	AggroList late;
	Add(late, hero, 5.f, AggroFlags::Alerted, 1.f);
	CHECK_FALSE(late.HasAggroed(now + 1249));
	CHECK(late.HasAggroed(now + 1250));
}

TEST_CASE("an ally alerted with a roll reacts after the alert delay") {
	AggroList ally;
	CHECK(ally.Alert(hero, now, 0.f) == AggroStimulus::FirstAlert);
	CHECK_FALSE(ally.HasAggroed(now + 499));
	CHECK(ally.HasAggroed(now + 500));

	AggroList slow;
	CHECK(slow.Alert(hero, now, 1.f) == AggroStimulus::FirstAlert);
	CHECK_FALSE(slow.HasAggroed(now + 1249));
	CHECK(slow.HasAggroed(now + 1250));
}

TEST_CASE("AlertObject lists an unlisted source with 5 and reacts at once") {
	AggroList list;
	CHECK(list.Alert(hero) == AggroStimulus::FirstAlert);
	CHECK(list.Get(hero) == doctest::Approx(AggroList::alertThreat));
	CHECK(list.HasAggroed(now));

	// A listed source gets nothing more, and a later one no reaction.
	CHECK(list.Alert(hero) == AggroStimulus::None);
	CHECK(list.Get(hero) == doctest::Approx(AggroList::alertThreat));
	CHECK(list.Alert(pet) == AggroStimulus::None);
	CHECK(list.Contains(pet));
}

TEST_CASE("an agent of aggroType 2 never aggroes, and only aggroType 0 is alerted") {
	AggroList never(2);
	CHECK(Add(never, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::None);
	CHECK(never.Alert(hero) == AggroStimulus::None);
	CHECK(never.IsEmpty());
	CHECK_FALSE(never.IsLatched());

	// The exported XML's aggroType held a name pointer; anything but 2 aggroes normally.
	CHECK(AggroList::AggroTypeFromData(74126312) == 0);
	CHECK(AggroList::AggroTypeFromData(2) == 2);
	AggroList corrupt(74126312);
	CHECK(corrupt.Alert(hero) == AggroStimulus::FirstAlert);
}

TEST_CASE("IgnoreFirstAggro takes the first reaction away") {
	AggroList list;
	list.IgnoreFirstAggro();
	CHECK(list.HasAggroed(now));
	CHECK(Add(list, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::None);
	CHECK(list.Contains(hero));
}

TEST_CASE("dropped entries are answered, and a list that emptied reacts again to a new one") {
	AggroList list;
	REQUIRE(Add(list, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::FirstAggro);
	Add(list, pet, 5.f);

	const auto removed = list.RemoveIf([](uint32_t id) { return id == pet; });
	CHECK(removed == std::vector<uint32_t> { pet });
	CHECK(list.GetIds() == std::vector<uint32_t> { hero });

	// Not empty yet: a new entry is no new fight.
	CHECK(Add(list, other, 5.f, AggroFlags::Perceived) == AggroStimulus::None);

	list.RemoveIf([](uint32_t) { return true; });
	CHECK(list.IsEmpty());
	CHECK(Add(list, hero, 5.f, AggroFlags::Perceived) == AggroStimulus::SubsequentAggro);
	CHECK(Add(list, pet, 5.f, AggroFlags::Perceived) == AggroStimulus::None);
}

TEST_CASE("the best target is the first entry that may be fought") {
	AggroList list;
	Add(list, 10, 50.f);
	Add(list, 11, 40.f);
	Add(list, 12, 30.f);
	Add(list, 13, 20.f);
	Add(list, 14, 10.f);
	Add(list, 15, 5.f);

	CHECK(list.BestTarget(Lookup(), false) == 10);

	AggroCandidate stealthed;
	stealthed.stealthed = true;
	AggroCandidate untargetable;
	untargetable.targetable = false;
	AggroCandidate scenery;
	scenery.combatant = false;
	AggroCandidate intangible;
	intangible.intangible = true;
	const std::map<uint32_t, std::optional<AggroCandidate>> unfit {
		{ 10, std::nullopt }, { 11, stealthed }, { 12, untargetable }, { 13, scenery }, { 14, intangible }
	};
	CHECK(list.BestTarget(Lookup(unfit), false) == 15);

	// An agent that detects stealth sees the stealthed one.
	CHECK(list.BestTarget(Lookup(unfit), true) == 11);

	// Something with no blackboard is never hidden by stealth, but cannot be targeted either.
	AggroCandidate noBlackboard;
	noBlackboard.hasBlackboard = false;
	noBlackboard.stealthed = true;
	CHECK(list.BestTarget(Lookup({ { 10, noBlackboard } }), false) == 11);

	CHECK(AggroList().BestTarget(Lookup(), false) == 0);
}
