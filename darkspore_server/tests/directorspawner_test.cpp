
// Include
#include "game/directorspawner.h"

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <vector>

using namespace Game;

namespace {
	constexpr uint32_t minionNoun = 0x1111;
	constexpr uint32_t specialNoun = 0x2222;

	// One minion type and one special type in every section, so a pick has a single answer.
	DirectorPool OneTypePool() {
		DirectorPool pool;
		for (auto& section : pool.sections) {
			section.minions.push_back({ minionNoun, 6, true });
			section.specials.push_back({ specialNoun, 30, true });
		}
		return pool;
	}

	const glm::vec3 far { 500.f, 0.f, 0.f };
}

TEST_CASE("a wander point a hero starts next to does not fire until the hero comes back") {
	DirectorSpawner spawner;
	spawner.AddWanderPoint({ 0.f, 0.f, 0.f }, 0);
	const auto pool = OneTypePool();

	// A spawn roll, a size of one, and no scatter.
	ScriptedDirectorRandom random({ 0.1f, 0.9f, 0.5f, 0.5f }, {});
	CHECK(spawner.Update({ { 5.f, 0.f, 0.f } }, true, pool, 1, random).empty());
	CHECK(random.UnitsLeft() == 4);

	CHECK(spawner.Update({ far }, true, pool, 1, random).empty());

	const auto spawns = spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random);
	REQUIRE(spawns.size() == 1);
	CHECK(spawns[0].noun == minionNoun);
	CHECK(spawns[0].position == glm::vec3(0.f, 0.f, 0.f));
	CHECK(std::string(spawns[0].source) == "wander");
}

TEST_CASE("a wander point fires at most once") {
	DirectorSpawner spawner;
	spawner.AddWanderPoint({ 0.f, 0.f, 0.f }, 1);
	const auto pool = OneTypePool();

	// The type pick is a Below() roll; with one minion type it is 0.
	ScriptedDirectorRandom random({ 0.1f, 0.9f, 0.5f, 0.5f }, { 0 });
	spawner.Update({ far }, true, pool, 1, random);
	CHECK(spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random).size() == 1);

	spawner.Update({ far }, true, pool, 1, random);
	CHECK(spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random).empty());
	CHECK(random.Overruns() == 0);
}

TEST_CASE("a missed first hit leaves the point for later hits at the higher chance") {
	DirectorSpawner spawner;
	spawner.AddWanderPoint({ 0.f, 0.f, 0.f }, 2);
	const auto pool = OneTypePool();

	// 0.2 misses the first hit's 18% and makes the second hit's 25%.
	ScriptedDirectorRandom random({ 0.2f, 0.2f, 0.9f, 0.5f, 0.5f }, {});
	spawner.Update({ far }, true, pool, 1, random);
	CHECK(spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random).empty());

	// Staying near is not another hit.
	CHECK(spawner.Update({ { 12.f, 0.f, 0.f } }, true, pool, 1, random).empty());
	CHECK(random.UnitsLeft() == 4);

	spawner.Update({ far }, true, pool, 1, random);
	CHECK(spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random).size() == 1);
}

TEST_CASE("a lucky size roll spawns a clump of wanderers around the point") {
	DirectorSpawner spawner;
	spawner.AddWanderPoint({ 40.f, 40.f, 2.f }, 0);
	const auto pool = OneTypePool();

	// 0.01 is under the 2% for four.
	ScriptedDirectorRandom random({ 0.1f, 0.01f, 0.f, 1.f, 0.5f, 0.5f, 0.25f, 0.75f, 0.5f, 0.5f }, {});
	spawner.Update({ far }, true, pool, 1, random);
	const auto spawns = spawner.Update({ { 40.f, 30.f, 2.f } }, true, pool, 1, random);
	REQUIRE(spawns.size() == 4);
	for (const auto& spawn : spawns) {
		CHECK(std::abs(spawn.position.x - 40.f) <= directorSpawnScatter);
		CHECK(std::abs(spawn.position.y - 40.f) <= directorSpawnScatter);
		CHECK(spawn.position.z == 2.f);
	}
	CHECK(spawns[0].position.x == doctest::Approx(40.f - directorSpawnScatter));
	CHECK(spawns[0].position.y == doctest::Approx(40.f + directorSpawnScatter));
}

TEST_CASE("a spike fires once and spreads its group over its points") {
	DirectorSpawner spawner;
	spawner.AddSpikeSet("spike_a", {
		{ { 0.f, 0.f, 0.f } },
		{ { 6.f, 0.f, 0.f } },
	}, 0);
	const auto pool = OneTypePool();

	DefaultDirectorRandom random;
	spawner.Update({ far }, true, pool, 1, random);
	const auto spawns = spawner.Update({ { 20.f, 0.f, 0.f } }, true, pool, 1, random);

	// From (Skip, Medium) a team doing well gets a Hard spike: section A's 50.
	CHECK(spawner.GetSpikeHistory().last == SpikeIntensity::Hard);
	REQUIRE(!spawns.empty());
	CHECK(spawns.size() <= directorMaxGroupSize);
	for (size_t i = 0; i < spawns.size(); ++i) {
		CHECK((spawns[i].noun == minionNoun || spawns[i].noun == specialNoun));
		const glm::vec3 point = (i % 2 == 0) ? glm::vec3(0.f, 0.f, 0.f) : glm::vec3(6.f, 0.f, 0.f);
		CHECK(std::abs(spawns[i].position.x - point.x) <= directorSpawnScatter);
		CHECK(std::abs(spawns[i].position.y - point.y) <= directorSpawnScatter);
		CHECK(std::string(spawns[i].point) == "spike_a");
	}

	spawner.Update({ far }, true, pool, 1, random);
	CHECK(spawner.Update({ { 20.f, 0.f, 0.f } }, true, pool, 1, random).empty());
}

TEST_CASE("a skipped spike spawns nothing but still counts in the history") {
	DirectorSpawner spawner;
	spawner.AddSpikeSet("first", { { { 0.f, 0.f, 0.f } } }, 0);
	spawner.AddSpikeSet("second", { { { 200.f, 0.f, 0.f } } }, 0);
	const auto pool = OneTypePool();

	DefaultDirectorRandom random;
	spawner.Update({ far }, true, pool, 1, random);
	CHECK(!spawner.Update({ { 10.f, 0.f, 0.f } }, true, pool, 1, random).empty());

	// After Hard, a team doing poorly gets Skip.
	CHECK(spawner.Update({ { 210.f, 0.f, 0.f } }, false, pool, 1, random).empty());
	CHECK(spawner.GetSpikeHistory().last == SpikeIntensity::Skip);
}

TEST_CASE("a spike set's section is the one most of its markers name") {
	CHECK(MostCommonSection({ 2, 2, 3, 3, 3 }) == 2);
	CHECK(MostCommonSection({ 1, 0, 1 }) == 1);
	CHECK(MostCommonSection({ 3, 3 }) == directorLevelSection);
	CHECK(MostCommonSection({}) == directorLevelSection);
	CHECK(MostCommonSection({ 7 }) == directorLevelSection);
}

TEST_CASE("an empty spike set is not kept") {
	DirectorSpawner spawner;
	spawner.AddSpikeSet("empty", {}, 0);
	CHECK(spawner.GetSpikeSetCount() == 0);
}
