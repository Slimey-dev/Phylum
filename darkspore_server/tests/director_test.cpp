#include <doctest/doctest.h>

#include "game/director.h"
#include "utils/functions.h"

#include <algorithm>
#include <vector>

using Game::DirectorBucket;
using Game::DirectorClassEntry;
using Game::DirectorEnemy;
using Game::DirectorGroup;
using Game::DirectorPool;
using Game::DirectorSection;
using Game::DirectorSpawnKind;
using Game::ScriptedDirectorRandom;
using Game::SpikeIntensity;

namespace {
	// Stand-in nouns. Only their identity matters.
	constexpr uint32_t minionA = 101, minionB = 102, minionC = 103, minionD = 104, minionE = 105;
	constexpr uint32_t specialA = 201, specialB = 202, specialC = 203;
	constexpr uint32_t agent = 301;

	DirectorClassEntry Entry(uint32_t noun, uint32_t minDifficulty, uint32_t maxDifficulty, int32_t challenge = 1) {
		return { noun, minDifficulty, maxDifficulty, true, challenge };
	}

	DirectorEnemy Enemy(uint32_t noun, int32_t challenge = 1) {
		return { noun, challenge, true };
	}

	std::vector<uint32_t> Nouns(const std::vector<DirectorEnemy>& enemies) {
		std::vector<uint32_t> nouns;
		for (const auto& enemy : enemies) {
			nouns.push_back(enemy.noun);
		}
		return nouns;
	}

	std::vector<float> Repeat(float value, size_t count) {
		return std::vector<float>(count, value);
	}

	// A mixed group's rolls: the specials roll, then one unused roll per pass of the loop.
	ScriptedDirectorRandom MixedGroupRolls(float specialsRoll, size_t passes, std::vector<uint32_t> picks = {}) {
		std::vector<float> units { specialsRoll };
		const auto unused = Repeat(0.f, passes);
		units.insert(units.end(), unused.begin(), unused.end());
		if (picks.empty()) {
			picks.assign(passes, 0);
		}
		return ScriptedDirectorRandom(units, picks);
	}

	// The level's types in 1-1's first-visit order.
	DirectorSection LevelSection() {
		DirectorSection level;
		level.minions = { Enemy(minionA), Enemy(minionB), Enemy(minionC) };
		level.specials = { Enemy(specialA), Enemy(specialB), Enemy(specialC) };
		return level;
	}
}

TEST_CASE("the director knows its spawn points by noun, as the client's 0x9fa9b0") {
	using Game::ClassifySpawnMarker;
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_Director.Noun")) == DirectorSpawnKind::Director);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorA.Noun")) == DirectorSpawnKind::A);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorB.Noun")) == DirectorSpawnKind::B);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorHorde.Noun")) == DirectorSpawnKind::Horde);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_HordeTrigger.Noun")) == DirectorSpawnKind::HordeTrigger);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorWanderer.Noun")) == DirectorSpawnKind::Wanderer);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorSpike.Noun")) == DirectorSpawnKind::Spike);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorBoss.Noun")) == DirectorSpawnKind::Boss);

	// The hashes the exe compares against
	CHECK(ClassifySpawnMarker(0xa3e463f5) == DirectorSpawnKind::Wanderer);
	CHECK(ClassifySpawnMarker(0x0218bcc7) == DirectorSpawnKind::Spike);
}

TEST_CASE("SpawnPoint_DirectorC and every other noun are not director spawn points") {
	using Game::ClassifySpawnMarker;
	CHECK(utils::hash_id("SpawnPoint_DirectorC.Noun") == 0xc9b5786c);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorC.Noun")) == DirectorSpawnKind::None);
	CHECK(ClassifySpawnMarker(utils::hash_id("TeleporterSpawnPoint.Noun")) == DirectorSpawnKind::None);
	CHECK(ClassifySpawnMarker(utils::hash_id("SpawnPoint_DirectorBossPortal.Noun")) == DirectorSpawnKind::None);
	CHECK(ClassifySpawnMarker(utils::hash_id("ZelemBasicRanged.Noun")) == DirectorSpawnKind::None);
	CHECK(ClassifySpawnMarker(0) == DirectorSpawnKind::None);
}

TEST_CASE("a scripted random hands out its values in order and counts what it lacked") {
	ScriptedDirectorRandom random({ 0.25f, 0.75f }, { 2, 9 });
	CHECK(random.Unit() == 0.25f);
	CHECK(random.Below(5) == 2);
	CHECK(random.Below(4) == 3); // 9 is held to the range
	CHECK(random.Below(0) == 0); // Nothing to choose: no value used
	CHECK(random.Unit() == 0.75f);
	CHECK(random.Overruns() == 0);

	CHECK(random.Unit() == 0.f);
	CHECK(random.Below(3) == 0);
	CHECK(random.Overruns() == 2);
}

TEST_CASE("the default random stays in its ranges") {
	Game::DefaultDirectorRandom random;
	for (int i = 0; i < 1000; ++i) {
		const float unit = random.Unit();
		CHECK(unit >= 0.f);
		CHECK(unit < 1.f);
		CHECK(random.Below(3) < 3);
	}
	CHECK(random.Below(0) == 0);
	CHECK(random.Below(1) == 0);
}

TEST_CASE("a first visit keeps the level's types of its difficulty, unshuffled when there are three") {
	// zelems_1's firstTimeConfig minions: three for each difficulty band
	const std::vector<DirectorClassEntry> minions {
		Entry(minionA, 1, 24), Entry(minionB, 1, 24), Entry(minionC, 1, 24),
		Entry(minionD, 25, 48), Entry(minionE, 49, 72)
	};
	const std::vector<DirectorClassEntry> specials { Entry(specialA, 1, 24), Entry(specialB, 25, 48) };

	ScriptedDirectorRandom random;
	const auto pool = Game::ChooseLevelPool(minions, specials, 1, random);
	CHECK(Nouns(pool.minions) == std::vector<uint32_t> { minionA, minionB, minionC });
	CHECK(Nouns(pool.specials) == std::vector<uint32_t> { specialA });
	CHECK(random.Overruns() == 0);

	const auto later = Game::ChooseLevelPool(minions, specials, 30, random);
	CHECK(Nouns(later.minions) == std::vector<uint32_t> { minionD });
	CHECK(Nouns(later.specials) == std::vector<uint32_t> { specialB });

	// Both ends of a range count
	CHECK(Game::ChooseLevelPool(minions, specials, 24, random).minions.size() == 3);
	CHECK(Game::ChooseLevelPool(minions, specials, 72, random).minions.size() == 1);
	CHECK(random.Overruns() == 0);
}

TEST_CASE("a longer list is shuffled as the client's 0x9fb530 and cut to three") {
	const std::vector<DirectorClassEntry> minions {
		Entry(minionA, 1, 72), Entry(minionB, 1, 72), Entry(minionC, 1, 72), Entry(minionD, 1, 72), Entry(minionE, 1, 72)
	};

	// Element i swaps with Below(i + 1): [A B C D E] -> [B A C D E] -> same -> [B D C A E] -> [E D C A B]
	ScriptedDirectorRandom random({}, { 0, 2, 1, 0 });
	const auto pool = Game::ChooseLevelPool(minions, {}, 1, random);
	CHECK(Nouns(pool.minions) == std::vector<uint32_t> { minionE, minionD, minionC });
	CHECK(pool.specials.empty());
	CHECK(random.PicksLeft() == 0);
	CHECK(random.Overruns() == 0);
}

TEST_CASE("the level pool keeps each noun once and carries its challenge") {
	const std::vector<DirectorClassEntry> minions { Entry(minionA, 1, 24, 6), Entry(minionA, 1, 72, 6), Entry(minionB, 1, 72, 5) };

	ScriptedDirectorRandom random;
	const auto pool = Game::ChooseLevelPool(minions, {}, 1, random);
	REQUIRE(pool.minions.size() == 2);
	CHECK(pool.minions[0].noun == minionA);
	CHECK(pool.minions[0].challenge == 6);
	CHECK(pool.minions[1].challenge == 5);
	CHECK(pool.minions[0].hordeLegal);
}

TEST_CASE("the default section buckets are the game's SectionConfig") {
	const auto buckets = Game::DefaultSectionBuckets();
	REQUIRE(buckets.size() == 72);

	for (size_t i = 0; i < buckets.size(); ++i) {
		CHECK(buckets[i].difficulty == i / 4 + 1);
		CHECK(buckets[i].numMinions == 2);
		CHECK(buckets[i].numSpecials == (i % 2 == 0 ? 1u : 2u));
		CHECK(buckets[i].chance == 0.f);
	}
}

TEST_CASE("sections A, B and C each take a different bucket and draw from a shared bag") {
	// Tier 1 buckets: 0 (2,1), 1 (2,2), 2 (2,1), 3 (2,2)
	ScriptedDirectorRandom random({}, {
		1, 2, 0, 1, 1, // A: bucket 1; minions C, A; specials B, C
		0, 0, 1, 0, // B: bucket 0 of [0 2 3]; minion B empties the bag, which refills: B again; special A
		1, 1, 0, 0, 0 // C: bucket 3 of [2 3]; minions C, A; the special bag refills: A, B
	});

	const auto level = LevelSection();
	const auto pool = Game::AllocateSections(level, Game::DefaultSectionBuckets(), 1, random);

	CHECK(Nouns(pool.sections[0].minions) == std::vector<uint32_t> { minionC, minionA });
	CHECK(Nouns(pool.sections[0].specials) == std::vector<uint32_t> { specialB, specialC });
	CHECK(Nouns(pool.sections[1].minions) == std::vector<uint32_t> { minionB, minionB });
	CHECK(Nouns(pool.sections[1].specials) == std::vector<uint32_t> { specialA });
	CHECK(Nouns(pool.sections[2].minions) == std::vector<uint32_t> { minionC, minionA });
	CHECK(Nouns(pool.sections[2].specials) == std::vector<uint32_t> { specialA, specialB });

	// Section 3 is the whole level
	CHECK(Nouns(pool.sections[3].minions) == Nouns(level.minions));
	CHECK(Nouns(pool.sections[3].specials) == Nouns(level.specials));

	CHECK(random.PicksLeft() == 0);
	CHECK(random.Overruns() == 0);
}

TEST_CASE("section C trades a minion for a special when its bucket has none") {
	const std::vector<DirectorBucket> buckets { { 3, 0, 1, 0 }, { 3, 0, 1, 0 }, { 3, 0, 1, 0 } };

	Game::DefaultDirectorRandom random;
	const auto pool = Game::AllocateSections(LevelSection(), buckets, 1, random);
	CHECK(pool.sections[0].minions.size() == 3);
	CHECK(pool.sections[0].specials.empty());
	CHECK(pool.sections[1].minions.size() == 3);
	CHECK(pool.sections[2].minions.size() == 2);
	CHECK(pool.sections[2].specials.size() == 1);
}

TEST_CASE("the bucket tier is the difficulty over four, rounded up, and needs three buckets") {
	const std::vector<DirectorBucket> buckets {
		{ 1, 1, 1, 0 }, { 1, 1, 1, 0 }, { 1, 1, 1, 0 },
		{ 2, 2, 2, 0 }, { 2, 2, 2, 0 }, { 2, 2, 2, 0 },
		{ 1, 1, 3, 0 }, { 1, 1, 3, 0 }
	};

	Game::DefaultDirectorRandom random;
	CHECK(Game::AllocateSections(LevelSection(), buckets, 4, random).sections[1].minions.size() == 1);
	CHECK(Game::AllocateSections(LevelSection(), buckets, 5, random).sections[1].minions.size() == 2);
	CHECK(Game::AllocateSections(LevelSection(), buckets, 8, random).sections[1].specials.size() == 2);

	// Tier 3 has two buckets: sections A, B and C stay empty, the level keeps its types
	const auto pool = Game::AllocateSections(LevelSection(), buckets, 9, random);
	for (size_t section = 0; section < 3; ++section) {
		CHECK(pool.sections[section].minions.empty());
		CHECK(pool.sections[section].specials.empty());
	}
	CHECK(pool.sections[3].minions.size() == 3);
}

TEST_CASE("a level without types leaves every section empty") {
	Game::DefaultDirectorRandom random;
	const auto pool = Game::AllocateSections({}, Game::DefaultSectionBuckets(), 1, random);
	for (const auto& section : pool.sections) {
		CHECK(section.minions.empty());
		CHECK(section.specials.empty());
	}
}

TEST_CASE("a group adds types while (1 + 0.2 per member) times its challenge fits the budget") {
	ScriptedDirectorRandom random;
	DirectorGroup group(30);
	std::vector<DirectorEnemy> candidates { Enemy(minionA, 10) };

	CHECK(group.Add(candidates, random)); // 1 * 10
	CHECK(group.Add(candidates, random)); // 1.2 * 20 = 24
	CHECK(group.GetCost() == doctest::Approx(24.f));
	CHECK_FALSE(group.Add(candidates, random)); // 1.4 * 30 = 42: too dear, so the type leaves the list
	CHECK(candidates.empty());

	CHECK(group.GetNouns() == std::vector<uint32_t> { minionA, minionA });
	CHECK(group.GetChallenge() == 20);
	CHECK_FALSE(group.IsBudgetSpent());
	CHECK_FALSE(group.Add(candidates, random));
}

TEST_CASE("a group works its cost out in single precision, as the client does") {
	// The sixth member costs (1 + 5 * 0.2f) * 60. In float 5 * 0.2f rounds to exactly 1, so it
	// costs 120 and fits a budget of 120; in double it would cost a little more and not fit.
	ScriptedDirectorRandom random;
	DirectorGroup group(120);
	std::vector<DirectorEnemy> candidates { Enemy(minionA, 10) };
	for (int i = 0; i < 6; ++i) {
		CHECK(group.Add(candidates, random));
	}
	CHECK(group.GetNouns().size() == 6);
	CHECK(group.IsBudgetSpent());
}

TEST_CASE("a too-dear pick takes out the first entry with that noun") {
	ScriptedDirectorRandom random({}, { 2 });
	DirectorGroup group(5);
	std::vector<DirectorEnemy> candidates { Enemy(minionA, 9), Enemy(minionB, 1), Enemy(minionA, 9) };

	CHECK_FALSE(group.Add(candidates, random));
	CHECK(Nouns(candidates) == std::vector<uint32_t> { minionB, minionA });
}

TEST_CASE("a group stops at fifteen members") {
	ScriptedDirectorRandom random;
	DirectorGroup group(1000);
	std::vector<DirectorEnemy> candidates { Enemy(minionA, 0) };
	for (int i = 0; i < 20; ++i) {
		group.Add(candidates, random);
	}
	CHECK(group.GetNouns().size() == 15);
	CHECK(group.IsFull());
}

TEST_CASE("agents need difficulty 7 and room left by the player count, as the client's 0x9faa40") {
	using Game::DirectorAgentAllowed;
	CHECK_FALSE(DirectorAgentAllowed(6, 4, 0));
	CHECK_FALSE(DirectorAgentAllowed(7, 1, 0));
	CHECK(DirectorAgentAllowed(7, 2, 0));
	CHECK_FALSE(DirectorAgentAllowed(7, 2, 1));
	CHECK_FALSE(DirectorAgentAllowed(7, 3, 1));
	CHECK(DirectorAgentAllowed(7, 4, 1));
	CHECK_FALSE(DirectorAgentAllowed(72, 4, 2));
}

TEST_CASE("a mixed group takes minions from its section, then also the next one, then the level") {
	DirectorPool pool;
	pool.sections[0].minions = { Enemy(minionA) };
	pool.sections[1].minions = { Enemy(minionB) };
	pool.sections[3].minions = { Enemy(minionC) };

	// No specials (roll 0.9). Fifteen passes; the four after the first four pick from [B A].
	std::vector<uint32_t> picks(15, 0);
	picks[4] = 0;
	picks[5] = 1;
	picks[6] = 0;
	picks[7] = 1;
	auto random = MixedGroupRolls(0.9f, 15, picks);

	const auto group = Game::BuildMixedGroup(pool, 0, 1000, 1, random);
	CHECK(group == std::vector<uint32_t> {
		minionA, minionA, minionA, minionA,
		minionB, minionA, minionB, minionA,
		minionC, minionC, minionC, minionC, minionC, minionC, minionC
	});
	CHECK(random.UnitsLeft() == 0);
	CHECK(random.PicksLeft() == 0);
	CHECK(random.Overruns() == 0);
}

TEST_CASE("section C's next section is A, and the whole level is its own") {
	DirectorPool pool;
	pool.sections[0].minions = { Enemy(minionA) };
	pool.sections[2].minions = { Enemy(minionC) };
	pool.sections[3].minions = { Enemy(minionD) };

	auto random = MixedGroupRolls(0.9f, 15);
	const auto group = Game::BuildMixedGroup(pool, 2, 1000, 1, random);
	REQUIRE(group.size() == 15);
	CHECK(group[3] == minionC);
	CHECK(group[4] == minionA); // [A C], first pick

	auto levelRandom = MixedGroupRolls(0.9f, 15);
	const auto levelGroup = Game::BuildMixedGroup(pool, 3, 1000, 1, levelRandom);
	CHECK(std::ranges::all_of(levelGroup, [](uint32_t noun) { return noun == minionD; }));
	CHECK(levelGroup.size() == 15);
}

TEST_CASE("the specials roll caps specials: 5% no cap, 15% two, 50% one, otherwise none") {
	DirectorPool pool;
	pool.sections[0].minions = { Enemy(minionA) };
	pool.sections[0].specials = { Enemy(specialA) };
	pool.sections[3].minions = { Enemy(minionA) };
	pool.sections[3].specials = { Enemy(specialC) };

	const auto build = [&](float specialsRoll) {
		auto random = MixedGroupRolls(specialsRoll, 15);
		const auto group = Game::BuildMixedGroup(pool, 0, 1000, 1, random);
		CHECK(random.Overruns() == 0);
		return group;
	};
	const auto count = [](const std::vector<uint32_t>& group, uint32_t noun) {
		return std::ranges::count(group, noun);
	};

	// No cap: two from the section, then the level's
	const auto unlimited = build(0.04f);
	CHECK(unlimited[0] == specialA);
	CHECK(unlimited[1] == specialA);
	CHECK(count(unlimited, specialC) == 13);

	const auto two = build(0.19f);
	CHECK(count(two, specialA) == 2);
	CHECK(count(two, minionA) == 13);

	const auto one = build(0.69f);
	CHECK(one[0] == specialA);
	CHECK(count(one, minionA) == 14);

	const auto none = build(0.71f);
	CHECK(count(none, minionA) == 15);
}

TEST_CASE("a mixed group stops once its budget is spent or its minions are too dear") {
	DirectorPool pool;
	pool.sections[0].minions = { Enemy(minionA, 10) };

	// 10, then 24; the third (42) is too dear and empties the list
	auto random = MixedGroupRolls(0.9f, 3);
	CHECK(Game::BuildMixedGroup(pool, 0, 30, 1, random) == std::vector<uint32_t> { minionA, minionA });
	CHECK(random.Overruns() == 0);

	// A budget of 0 is spent before the first member
	ScriptedDirectorRandom broke({ 0.9f }, {});
	CHECK(Game::BuildMixedGroup(pool, 0, 0, 1, broke).empty());
	CHECK(broke.Overruns() == 0);

	// No minions in the section: nothing, even with specials
	DirectorPool specialsOnly;
	specialsOnly.sections[0].specials = { Enemy(specialA) };
	ScriptedDirectorRandom empty({ 0.04f }, {});
	CHECK(Game::BuildMixedGroup(specialsOnly, 0, 1000, 1, empty).empty());
}

TEST_CASE("agents join a mixed group on a 10% roll, only when allowed and from difficulty 7") {
	DirectorPool pool;
	pool.sections[0].minions = { Enemy(minionA) };
	pool.sections[0].specials = { Enemy(specialA) };
	pool.sections[3].specials = { Enemy(specialC) };
	const std::vector<DirectorClassEntry> agents { Entry(agent, 7, 72) };

	// No cap on specials, so every pass may take an agent: 0.05 does, 0.5 does not
	std::vector<float> units { 0.04f };
	for (int pass = 0; pass < 15; ++pass) {
		units.push_back(0.f);
		units.push_back(pass == 0 || pass == 3 ? 0.05f : 0.5f);
	}
	ScriptedDirectorRandom random(units, std::vector<uint32_t>(15, 0));
	const auto group = Game::BuildMixedGroup(pool, 0, 1000, 7, random, agents, true);
	REQUIRE(group.size() == 15);
	CHECK(group[0] == agent);
	CHECK(group[1] == specialA);
	CHECK(group[2] == specialA);
	CHECK(group[3] == agent);
	CHECK(group[4] == specialC);
	CHECK(random.UnitsLeft() == 0);
	CHECK(random.Overruns() == 0);

	// Not allowed: no agent roll at all
	auto notAllowed = MixedGroupRolls(0.04f, 15);
	const auto withoutAgents = Game::BuildMixedGroup(pool, 0, 1000, 7, notAllowed, agents, false);
	CHECK(std::ranges::count(withoutAgents, agent) == 0);
	CHECK(notAllowed.UnitsLeft() == 0);

	// Below difficulty 7 the agent's own range already leaves it out
	auto early = MixedGroupRolls(0.04f, 15);
	CHECK(std::ranges::count(Game::BuildMixedGroup(pool, 0, 1000, 6, early, agents, true), agent) == 0);

	// An agent for every difficulty still waits for 7; the client rolls first and checks after
	std::vector<float> earlyUnits { 0.04f };
	for (int pass = 0; pass < 15; ++pass) {
		earlyUnits.push_back(0.f);
		earlyUnits.push_back(0.05f);
	}
	ScriptedDirectorRandom earlyRolls(earlyUnits, std::vector<uint32_t>(15, 0));
	const auto anyDifficulty = Game::BuildMixedGroup(pool, 0, 1000, 6, earlyRolls, { Entry(agent, 1, 72) }, true);
	CHECK(std::ranges::count(anyDifficulty, agent) == 0);
	CHECK(earlyRolls.UnitsLeft() == 0);
}

TEST_CASE("a wanderer is one of the section's minions, then the level's, then a special") {
	DirectorPool pool;
	pool.sections[1].minions = { Enemy(minionA), Enemy(minionB) };
	pool.sections[3].minions = { Enemy(minionC) };
	pool.sections[2].specials = { Enemy(specialA) };
	pool.sections[3].specials = { Enemy(specialC) };

	ScriptedDirectorRandom random({}, { 1, 0, 0, 0, 0 });
	CHECK(Game::PickWanderer(pool, 1, random) == minionB);
	CHECK(Game::PickWanderer(pool, 0, random) == minionC);
	CHECK(Game::PickWanderer(pool, 9, random) == minionC); // Past the sections: the whole level

	pool.sections[3].minions.clear();
	CHECK(Game::PickWanderer(pool, 2, random) == specialA);
	CHECK(Game::PickWanderer(pool, 0, random) == specialC);
	CHECK(Game::PickWanderer({}, 0, random) == 0);
}

TEST_CASE("a wander point spawns on 18% of first hits and 25% of hits 2 to 5 (approximate)") {
	using Game::RollWanderHit;
	ScriptedDirectorRandom random({
		0.17f, 0.5f, // First hit: spawns one
		0.18f, // First hit: nothing, and no size roll
		0.24f, 0.5f, // Second hit: spawns
		0.25f, // Third hit: nothing
		0.24f, 0.5f // Fifth hit: spawns
	}, {});
	CHECK(RollWanderHit(0, random) == 1);
	CHECK(RollWanderHit(0, random) == 0);
	CHECK(RollWanderHit(1, random) == 1);
	CHECK(RollWanderHit(2, random) == 0);
	CHECK(RollWanderHit(4, random) == 1);
	CHECK(random.UnitsLeft() == 0);

	// After the fifth hit there is no roll
	CHECK(RollWanderHit(5, random) == 0);
	CHECK(random.Overruns() == 0);
}

TEST_CASE("a wanderer spawn is 2, 3 or 4 wanderers 12, 6 and 2% of the time (approximate)") {
	using Game::RollWanderHit;
	ScriptedDirectorRandom random({ 0.f, 0.01f, 0.f, 0.05f, 0.f, 0.15f, 0.f, 0.21f }, {});
	CHECK(RollWanderHit(0, random) == 4);
	CHECK(RollWanderHit(0, random) == 3);
	CHECK(RollWanderHit(0, random) == 2);
	CHECK(RollWanderHit(0, random) == 1);
}

TEST_CASE("the next spike follows the talk's table of the last two (approximate)") {
	using Game::NextSpikeIntensity;
	using enum SpikeIntensity;

	struct Row {
		SpikeIntensity beforeLast, last, well, poorly;
	};
	const std::vector<Row> table {
		{ Hard, Medium, Hard, Medium },
		{ Hard, Skip, Hard, Medium },
		{ Medium, Hard, Medium, Skip },
		{ Medium, Medium, Hard, Skip },
		{ Medium, Skip, Hard, Medium },
		{ Skip, Hard, Medium, Skip },
		{ Skip, Medium, Hard, Medium }
	};
	for (const auto& row : table) {
		CHECK(NextSpikeIntensity(row.beforeLast, row.last, true) == row.well);
		CHECK(NextSpikeIntensity(row.beforeLast, row.last, false) == row.poorly);
	}

	// Pairs the table never reaches act as the opening (Skip, Medium)
	CHECK(NextSpikeIntensity(Hard, Hard, true) == Hard);
	CHECK(NextSpikeIntensity(Skip, Skip, false) == Medium);
}

TEST_CASE("a run's spike history starts as (Skip, Medium) and moves on") {
	Game::SpikeHistory history;
	CHECK(history.Advance(true) == SpikeIntensity::Hard); // (Skip, Medium)
	CHECK(history.Advance(false) == SpikeIntensity::Skip); // (Medium, Hard)
	CHECK(history.Advance(true) == SpikeIntensity::Hard); // (Hard, Skip)
	CHECK(history.beforeLast == SpikeIntensity::Skip);
	CHECK(history.last == SpikeIntensity::Hard);
}

TEST_CASE("spike budgets rise through the level, Medium is half and Skip nothing (approximate)") {
	using Game::SpikeBudget;
	CHECK(SpikeBudget(0, SpikeIntensity::Hard, 1) == 50);
	CHECK(SpikeBudget(1, SpikeIntensity::Hard, 1) == 80);
	CHECK(SpikeBudget(2, SpikeIntensity::Hard, 1) == 110);
	CHECK(SpikeBudget(3, SpikeIntensity::Hard, 1) == 80);
	CHECK(SpikeBudget(0, SpikeIntensity::Medium, 1) == 25);
	CHECK(SpikeBudget(2, SpikeIntensity::Medium, 1) == 55);
	CHECK(SpikeBudget(1, SpikeIntensity::Skip, 1) == 0);

	// No difficulty scaling yet
	CHECK(SpikeBudget(2, SpikeIntensity::Hard, 72) == 110);
}

TEST_CASE("with real rolls every group stays within its budget and the level's types") {
	const std::vector<DirectorClassEntry> minions {
		Entry(minionA, 1, 24, 6), Entry(minionB, 1, 24, 6), Entry(minionC, 1, 24, 4), Entry(minionD, 1, 24, 5)
	};
	const std::vector<DirectorClassEntry> specials { Entry(specialA, 1, 24, 30), Entry(specialB, 1, 24, 27), Entry(specialC, 1, 24, 30) };

	Game::DefaultDirectorRandom random;
	for (int run = 0; run < 200; ++run) {
		const auto level = Game::ChooseLevelPool(minions, specials, 1, random);
		REQUIRE(level.minions.size() == 3);
		REQUIRE(level.specials.size() == 3);

		const auto pool = Game::AllocateSections(level, Game::DefaultSectionBuckets(), 1, random);
		for (uint32_t section = 0; section < 3; ++section) {
			CHECK(pool.sections[section].minions.size() == 2);
			CHECK(pool.sections[section].specials.size() >= 1);
		}

		for (uint32_t section = 0; section < 4; ++section) {
			const int32_t budget = Game::SpikeBudget(section, SpikeIntensity::Hard, 1);
			const auto group = Game::BuildMixedGroup(pool, section, budget, 1, random);
			CHECK(group.size() <= Game::directorMaxGroupSize);

			// Replay the group's cost: every member had to fit
			int32_t challenge = 0;
			for (size_t i = 0; i < group.size(); ++i) {
				const auto isNoun = [&](const DirectorEnemy& enemy) { return enemy.noun == group[i]; };
				const auto minion = std::ranges::find_if(level.minions, isNoun);
				const auto special = std::ranges::find_if(level.specials, isNoun);
				REQUIRE((minion != level.minions.end() || special != level.specials.end()));
				challenge += minion != level.minions.end() ? minion->challenge : special->challenge;
				CHECK((static_cast<float>(i) * 0.2f + 1.f) * static_cast<float>(challenge) <= static_cast<float>(budget));
			}
		}
	}
}
