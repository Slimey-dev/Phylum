
// Include
#include "director.h"

#include "utils/functions.h"

#include <algorithm>
#include <cmath>
#include <utility>

// Game
namespace Game {
	namespace {
		// Tuning the client loads from its prop file (group and instance 0x3b01d7f6 in ServerData.package)
		// in 0x9cf190. These are the file's values; the defaults compiled into the exe differ.
		constexpr float groupCostStep = 0.2f; // 0x14cb084, prop 2ccf993d: each member makes the next 20% dearer
		constexpr bool addSpecialFirst = false; // 0x14cafe7, prop 65648990: a mixed group may start with a special
		constexpr float unlimitedSpecialsChance = 0.05f; // 0x14cb070, prop a547e347
		constexpr float twoSpecialsChance = 0.15f; // 0x14cb06c, prop 5b780357
		constexpr float oneSpecialChance = 0.5f; // 0x14cb068, prop 61735c75. Otherwise none
		constexpr float agentChance = 0.1f; // 0x14cb074, prop df7e68d1
		constexpr int32_t sectionSpecials = 2; // 0x14cb078, prop 2edb3805: specials taken from the section, then from the level
		constexpr int32_t ownSectionMinions = 4; // 0x14cb07c, prop e0da6cd6: minions taken from the section alone
		constexpr int32_t neighbourMinions = 8; // 0x14cb080, prop 9931d732: then from it and the next section, then from the level
		constexpr uint32_t agentMinDifficulty = 7; // 0x1164ba4, prop dbae1599

		// What "no limit" on specials is in 0x9fd660 (the float at 0x1025090).
		constexpr float unlimitedSpecials = 99.f;

		// AllocateSections gives sections A, B and C nothing unless the tier has more than two buckets.
		constexpr size_t minTierBuckets = 3;
		constexpr uint32_t routeSections = 3;

		// Wander points, from the director designer's GDC 2012 talk (Daniel Kline, "The AI Director of
		// Darkspore"). Approximate: the talk gives the chances, not how the game rolled them.
		constexpr uint32_t wanderHitLimit = 5; // No spawn after the 5th hit
		constexpr float wanderFirstHitChance = 0.18f;
		constexpr float wanderLaterHitChance = 0.25f; // Hits 2 to 5
		constexpr float wanderFourChance = 0.02f; // A spawn is 4 wanderers instead of 1
		constexpr float wanderThreeChance = 0.06f;
		constexpr float wanderTwoChance = 0.12f;

		// The challenge budget of a Hard spike per section (A, B, C, any). Approximate: darkspin's
		// local fallback (github.com/darkspinnet/darkspin, MIT license), which uses B's for "any".
		// The talk only says budgets rise through a level and Medium is about half of Hard.
		constexpr std::array<int32_t, directorSectionCount> spikeHardBudget { 50, 80, 110, 80 };

		uint32_t ClampSection(uint32_t section) {
			return std::min(section, directorLevelSection);
		}

		uint32_t Pick(DirectorRandom& random, size_t count) {
			return std::min(random.Below(static_cast<uint32_t>(count)), static_cast<uint32_t>(count - 1));
		}

		DirectorEnemy ToEnemy(const DirectorClassEntry& entry) {
			return { entry.noun, entry.challenge, entry.hordeLegal };
		}

		bool InRange(const DirectorClassEntry& entry, uint32_t difficulty) {
			return entry.minDifficulty <= difficulty && difficulty <= entry.maxDifficulty;
		}

		// Shuffles as the client's 0x9fb530: each element from the second on swaps with a random one
		// at or before it.
		void Shuffle(std::vector<DirectorEnemy>& list, DirectorRandom& random) {
			for (size_t i = 1; i < list.size(); ++i) {
				std::swap(list[i], list[Pick(random, i + 1)]);
			}
		}

		// The entries of the difficulty, shuffled and cut to the pool size, as 0x9fbd10 does for minions
		// and then specials. It shuffles only a list longer than it keeps.
		// Phylum also keeps each noun once; the client does not, but no shipped firstTimeConfig lists
		// a noun twice for the same difficulty.
		std::vector<DirectorEnemy> ChooseTypes(const std::vector<DirectorClassEntry>& entries, uint32_t difficulty, DirectorRandom& random) {
			std::vector<DirectorEnemy> types;
			for (const auto& entry : entries) {
				const bool known = std::ranges::any_of(types, [&](const DirectorEnemy& type) { return type.noun == entry.noun; });
				if (InRange(entry, difficulty) && !known) {
					types.push_back(ToEnemy(entry));
				}
			}

			if (types.size() > directorLevelPoolSize) {
				Shuffle(types, random);
				types.resize(directorLevelPoolSize);
			}
			return types;
		}

		// Draws count of the level's types for one section, as 0x9fc430 does. The bag holds indices
		// into types, is shared by sections A, B and C, and refills with every type when it runs dry,
		// so a section can get the same type twice.
		void DrawFromBag(const std::vector<DirectorEnemy>& types, std::vector<uint32_t>& bag, int32_t count,
			std::vector<DirectorEnemy>& section, DirectorRandom& random
		) {
			for (int32_t i = 0; i < count; ++i) {
				if (bag.empty()) {
					for (uint32_t type = 0; type < types.size(); ++type) {
						bag.push_back(type);
					}
					if (bag.empty()) {
						continue;
					}
				}

				const auto drawn = bag.begin() + Pick(random, bag.size());
				section.push_back(types[*drawn]);
				bag.erase(drawn);
			}
		}
	}

	// DefaultDirectorRandom
	DefaultDirectorRandom::DefaultDirectorRandom() : DefaultDirectorRandom(std::random_device {}()) {}

	DefaultDirectorRandom::DefaultDirectorRandom(uint32_t seed) : mSeed(seed), mGenerator(seed) {}

	float DefaultDirectorRandom::Unit() {
		// uniform_real_distribution can round up to 1 itself
		const float value = std::uniform_real_distribution<float>(0.f, 1.f)(mGenerator);
		return value < 1.f ? value : std::nextafter(1.f, 0.f);
	}

	uint32_t DefaultDirectorRandom::Below(uint32_t count) {
		return count == 0 ? 0 : std::uniform_int_distribution<uint32_t>(0, count - 1)(mGenerator);
	}

	uint32_t DefaultDirectorRandom::GetSeed() const {
		return mSeed;
	}

	// ScriptedDirectorRandom
	ScriptedDirectorRandom::ScriptedDirectorRandom(std::vector<float> units, std::vector<uint32_t> picks)
		: mUnits(units.begin(), units.end()), mPicks(picks.begin(), picks.end()) {}

	void ScriptedDirectorRandom::PushUnit(float value) {
		mUnits.push_back(value);
	}

	void ScriptedDirectorRandom::PushBelow(uint32_t value) {
		mPicks.push_back(value);
	}

	float ScriptedDirectorRandom::Unit() {
		if (mUnits.empty()) {
			++mOverruns;
			return 0;
		}

		const float value = mUnits.front();
		mUnits.pop_front();
		return value;
	}

	uint32_t ScriptedDirectorRandom::Below(uint32_t count) {
		// Nothing to choose from: no scripted value is used up
		if (count == 0) {
			return 0;
		}
		if (mPicks.empty()) {
			++mOverruns;
			return 0;
		}

		const uint32_t value = mPicks.front();
		mPicks.pop_front();
		return std::min(value, count - 1);
	}

	size_t ScriptedDirectorRandom::UnitsLeft() const {
		return mUnits.size();
	}

	size_t ScriptedDirectorRandom::PicksLeft() const {
		return mPicks.size();
	}

	uint32_t ScriptedDirectorRandom::Overruns() const {
		return mOverruns;
	}

	// ClassifySpawnMarker
	DirectorSpawnKind ClassifySpawnMarker(uint32_t nounHash) {
		// 0x9fa9b0 compares the noun id (noun + 0x5c) against these hashes
		switch (nounHash) {
			case utils::hash_id("SpawnPoint_Director.Noun"): return DirectorSpawnKind::Director; // 0x08911eb7
			case utils::hash_id("SpawnPoint_DirectorA.Noun"): return DirectorSpawnKind::A; // 0x0505407e
			case utils::hash_id("SpawnPoint_DirectorB.Noun"): return DirectorSpawnKind::B; // 0x33394477
			case utils::hash_id("SpawnPoint_DirectorHorde.Noun"): return DirectorSpawnKind::Horde; // 0x45e6b07b
			case utils::hash_id("SpawnPoint_HordeTrigger.Noun"): return DirectorSpawnKind::HordeTrigger; // 0xc70911c5
			case utils::hash_id("SpawnPoint_DirectorWanderer.Noun"): return DirectorSpawnKind::Wanderer; // 0xa3e463f5
			case utils::hash_id("SpawnPoint_DirectorSpike.Noun"): return DirectorSpawnKind::Spike; // 0x0218bcc7
			case utils::hash_id("SpawnPoint_DirectorBoss.Noun"): return DirectorSpawnKind::Boss; // 0xd12a4efe

			// The client also counts the id 4 as SpawnPoint_Director (cmp eax,4). No noun name is
			// known to hash to it, so it is likely a leftover.
			case 4: return DirectorSpawnKind::Director;

			// SpawnPoint_DirectorC (0xc9b5786c) appears nowhere in the exe
			default: return DirectorSpawnKind::None;
		}
	}

	// ChooseLevelPool
	DirectorSection ChooseLevelPool(const std::vector<DirectorClassEntry>& minionEntries,
		const std::vector<DirectorClassEntry>& specialEntries, uint32_t difficulty, DirectorRandom& random
	) {
		// 0x9fc430 uses this when a player has not reached the difficulty yet (player + 0x125c), so on
		// every first visit. The repeat-visit pool (0x9fc150, levelConfig, planetConfig and element
		// configs) is not ported.
		DirectorSection pool;
		pool.minions = ChooseTypes(minionEntries, difficulty, random);
		pool.specials = ChooseTypes(specialEntries, difficulty, random);
		return pool;
	}

	// DefaultSectionBuckets
	std::vector<DirectorBucket> DefaultSectionBuckets() {
		// SectionConfig in Data/AssetData_Binary.package (type 0xa38fa119, instance 0x45b7b07c,
		// 8 + 72 * 16 bytes): for each tier 1 to 18, four buckets of (minions, specials)
		// (2, 1), (2, 2), (2, 1), (2, 2), all with chance 0. The exported SectionConfig XML is
		// misparsed (72 empty names and weights), so the buckets are written out here.
		constexpr uint32_t tiers = 18;
		constexpr std::array<std::pair<uint32_t, uint32_t>, 4> tierBuckets {{ { 2, 1 }, { 2, 2 }, { 2, 1 }, { 2, 2 } }};

		std::vector<DirectorBucket> buckets;
		buckets.reserve(tiers * tierBuckets.size());
		for (uint32_t tier = 1; tier <= tiers; ++tier) {
			for (const auto& [minions, specials] : tierBuckets) {
				buckets.push_back({ minions, specials, tier, 0.f });
			}
		}
		return buckets;
	}

	// AllocateSections
	DirectorPool AllocateSections(const DirectorSection& levelSection, const std::vector<DirectorBucket>& buckets,
		uint32_t difficulty, DirectorRandom& random
	) {
		DirectorPool pool;
		pool.sections[directorLevelSection] = levelSection;

		// The tier of a difficulty (1 or more) is ceil(difficulty / 4), as 0x9bd520 works it out
		const uint32_t tier = (difficulty + 3) / 4;

		std::vector<size_t> tierBuckets;
		for (size_t i = 0; i < buckets.size(); ++i) {
			if (buckets[i].difficulty == tier) {
				tierBuckets.push_back(i);
			}
		}
		if (tierBuckets.size() < minTierBuckets) {
			return pool;
		}

		// The client refills a bag with as many indices as its candidate list had before the cut to 3.
		// On the first-visit path that list is never longer than 3, so it is the level section here.
		std::vector<uint32_t> minionBag;
		std::vector<uint32_t> specialBag;
		for (uint32_t section = 0; section < routeSections; ++section) {
			const size_t chosen = Pick(random, tierBuckets.size());
			const DirectorBucket& bucket = buckets[tierBuckets[chosen]];

			auto numMinions = static_cast<int32_t>(bucket.numMinions);
			auto numSpecials = static_cast<int32_t>(bucket.numSpecials);

			// Section C always gets a special: a bucket without one gives up a minion for it
			if (section == 2 && numSpecials == 0) {
				numSpecials = 1;
				--numMinions;
			}

			DrawFromBag(levelSection.minions, minionBag, numMinions, pool.sections[section].minions, random);
			DrawFromBag(levelSection.specials, specialBag, numSpecials, pool.sections[section].specials, random);

			// Each section gets a different bucket
			tierBuckets.erase(tierBuckets.begin() + chosen);
		}
		return pool;
	}

	// DirectorGroup
	DirectorGroup::DirectorGroup(int32_t budget) : mBudget(budget) {}

	bool DirectorGroup::Add(std::vector<DirectorEnemy>& candidates, DirectorRandom& random) {
		// The client's callers stop at 15 members before they call it
		if (candidates.empty() || IsFull()) {
			return false;
		}

		const DirectorEnemy candidate = candidates[Pick(random, candidates.size())];

		// cost = (1 + 0.2 * members) * (group challenge + this type's challenge). The client works it
		// out in single precision (0x9fbaff-0x9fbb26), and so does this.
		const float cost = (static_cast<float>(mNouns.size()) * groupCostStep + 1.f) * static_cast<float>(mChallenge + candidate.challenge);
		if (cost <= static_cast<float>(mBudget)) {
			mNouns.push_back(candidate.noun);
			mChallenge += candidate.challenge;
			mCost = cost;
			return true;
		}

		// Too dear: the type is out of the running for this group. The client takes out the first
		// entry with that noun, which matters only when a list holds a type twice.
		const auto it = std::ranges::find_if(candidates, [&](const DirectorEnemy& entry) { return entry.noun == candidate.noun; });
		candidates.erase(it);
		return false;
	}

	bool DirectorGroup::IsFull() const {
		return mNouns.size() >= directorMaxGroupSize;
	}

	bool DirectorGroup::IsBudgetSpent() const {
		return static_cast<float>(mBudget) <= mCost;
	}

	const std::vector<uint32_t>& DirectorGroup::GetNouns() const {
		return mNouns;
	}

	int32_t DirectorGroup::GetBudget() const {
		return mBudget;
	}

	int32_t DirectorGroup::GetChallenge() const {
		return mChallenge;
	}

	float DirectorGroup::GetCost() const {
		return mCost;
	}

	// DirectorAgentAllowed
	bool DirectorAgentAllowed(uint32_t difficulty, uint32_t playersWithCreature, uint32_t agents) {
		// 0x9faa40 first checks a value that is most likely the difficulty (0x9bcc50)
		if (difficulty < agentMinDifficulty) {
			return false;
		}

		// Room for one agent with two players and two fewer than the players above that, so none
		// for a single player
		const int64_t limit = playersWithCreature == 2 ? 1 : static_cast<int64_t>(playersWithCreature) - 2;
		return static_cast<int64_t>(agents) < limit;
	}

	// BuildMixedGroup
	std::vector<uint32_t> BuildMixedGroup(const DirectorPool& pool, uint32_t section, int32_t budget,
		uint32_t difficulty, DirectorRandom& random,
		const std::vector<DirectorClassEntry>& agentEntries, bool agentAllowed
	) {
		// The client also filters every section list by difficulty here. The pool was built for one
		// difficulty and DirectorEnemy keeps no range, so that is left out.
		section = ClampSection(section);
		const DirectorSection& own = pool.sections[section];
		const DirectorSection& level = pool.sections[directorLevelSection];

		// The next section (A to B, B to C, C to A). The whole level is its own neighbour.
		const uint32_t neighbour = section == directorLevelSection ? directorLevelSection : (section + 1) % routeSections;

		std::vector<DirectorEnemy> ownMinions = own.minions;
		std::vector<DirectorEnemy> nearMinions = pool.sections[neighbour].minions;
		nearMinions.insert(nearMinions.end(), own.minions.begin(), own.minions.end());
		std::vector<DirectorEnemy> levelMinions = level.minions;
		std::vector<DirectorEnemy> ownSpecials = own.specials;
		std::vector<DirectorEnemy> levelSpecials = level.specials;

		std::vector<DirectorEnemy> agents;
		for (const auto& entry : agentEntries) {
			if (InRange(entry, difficulty)) {
				agents.push_back(ToEnemy(entry));
			}
		}

		DirectorGroup group(budget);
		int32_t minionsAdded = 0;
		int32_t specialsAdded = 0;
		int32_t agentsAdded = 0;

		if (addSpecialFirst && group.Add(ownSpecials, random)) {
			specialsAdded = 1;
		}

		// How many specials and agents the group may have
		float maxSpecials = 0;
		const float specialsRoll = random.Unit();
		if (specialsRoll < unlimitedSpecialsChance) {
			maxSpecials = unlimitedSpecials;
		} else if (specialsRoll < twoSpecialsChance + unlimitedSpecialsChance) {
			maxSpecials = 2;
		} else if (specialsRoll < oneSpecialChance + twoSpecialsChance + unlimitedSpecialsChance) {
			maxSpecials = 1;
		} else {
			// None: the client empties the section's specials
			ownSpecials.clear();
		}

		// Ends when the group is full, has spent its budget, or the minion list it used last is empty
		std::vector<DirectorEnemy>* minions = &ownMinions;
		while (!group.IsFull() && !group.IsBudgetSpent() && !minions->empty()) {
			// The client rolls a number here and never uses it (0x9fdad2)
			random.Unit();

			std::vector<DirectorEnemy>* specials = specialsAdded < sectionSpecials ? &ownSpecials : &levelSpecials;
			if (minionsAdded < ownSectionMinions) {
				minions = &ownMinions;
			} else if (minionsAdded < neighbourMinions) {
				minions = &nearMinions;
			} else {
				minions = &levelMinions;
			}

			if (specials->empty() || maxSpecials <= static_cast<float>(agentsAdded + specialsAdded)) {
				if (!minions->empty() && group.Add(*minions, random)) {
					++minionsAdded;
				}
			} else if (agents.empty() || !agentAllowed || random.Unit() >= agentChance || difficulty < agentMinDifficulty) {
				if (group.Add(*specials, random)) {
					++specialsAdded;
				}
			} else if (group.Add(agents, random)) {
				++agentsAdded;
			}
		}
		return group.GetNouns();
	}

	// PickWanderer
	uint32_t PickWanderer(const DirectorPool& pool, uint32_t section, DirectorRandom& random) {
		// 0x9fe270 type 1 picks one of the section's minions through AddToGroup, so it also checks the
		// type against a budget; without one every type fits. Falling back to the level's minions and
		// then to specials is Phylum's: the client spawns nothing when the section has no minions.
		section = ClampSection(section);
		const DirectorSection& own = pool.sections[section];
		const DirectorSection& level = pool.sections[directorLevelSection];

		for (const auto* types : { &own.minions, &level.minions, &own.specials, &level.specials }) {
			if (!types->empty()) {
				return (*types)[Pick(random, types->size())].noun;
			}
		}
		return 0;
	}

	// RollWanderHit
	uint32_t RollWanderHit(uint32_t hitIndex, DirectorRandom& random) {
		// Approximate: the talk gives 18% for the first hit and 25% for the 2nd to 5th, with a 12, 6
		// and 2% chance of 2, 3 or 4 wanderers. Which hits count (per point or per run) is the caller's.
		if (hitIndex >= wanderHitLimit) {
			return 0;
		}

		const float chance = hitIndex == 0 ? wanderFirstHitChance : wanderLaterHitChance;
		if (random.Unit() >= chance) {
			return 0;
		}

		const float sizeRoll = random.Unit();
		if (sizeRoll < wanderFourChance) {
			return 4;
		}
		if (sizeRoll < wanderFourChance + wanderThreeChance) {
			return 3;
		}
		if (sizeRoll < wanderFourChance + wanderThreeChance + wanderTwoChance) {
			return 2;
		}
		return 1;
	}

	// NextSpikeIntensity
	SpikeIntensity NextSpikeIntensity(SpikeIntensity beforeLast, SpikeIntensity last, bool doingWell) {
		// Approximate: the talk's table of the last two spikes and the next one when the players are
		// doing well or poorly. It never has two Hard or two Skip spikes in a row, so it has no row
		// for those; they act as the opening (Skip, Medium).
		struct Row {
			SpikeIntensity beforeLast;
			SpikeIntensity last;
			SpikeIntensity well;
			SpikeIntensity poorly;
		};

		using enum SpikeIntensity;
		constexpr std::array<Row, 7> table {{
			{ Hard, Medium, Hard, Medium },
			{ Hard, Skip, Hard, Medium },
			{ Medium, Hard, Medium, Skip },
			{ Medium, Medium, Hard, Skip },
			{ Medium, Skip, Hard, Medium },
			{ Skip, Hard, Medium, Skip },
			{ Skip, Medium, Hard, Medium }
		}};

		for (const auto& row : table) {
			if (row.beforeLast == beforeLast && row.last == last) {
				return doingWell ? row.well : row.poorly;
			}
		}
		return doingWell ? Hard : Medium;
	}

	// SpikeHistory
	SpikeIntensity SpikeHistory::Advance(bool doingWell) {
		const SpikeIntensity next = NextSpikeIntensity(beforeLast, last, doingWell);
		beforeLast = last;
		last = next;
		return next;
	}

	// SpikeBudget
	int32_t SpikeBudget(uint32_t section, SpikeIntensity intensity, uint32_t difficulty) {
		// Difficulty scaling goes here once something says how budgets grow with the chain level.
		// Nothing does yet, so every difficulty gets the base.
		static_cast<void>(difficulty);

		const int32_t hard = spikeHardBudget[ClampSection(section)];
		switch (intensity) {
			case SpikeIntensity::Hard: return hard;
			case SpikeIntensity::Medium: return hard / 2;
			default: return 0;
		}
	}
}
