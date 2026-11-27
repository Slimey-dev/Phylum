
#ifndef _GAME_DIRECTOR_HEADER
#define _GAME_DIRECTOR_HEADER

// Include
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

// Game
namespace Game {
	// The AI director's building blocks: which enemies a level uses, how they are split over the
	// level's sections, and how a group is put together against a challenge budget. Each piece is
	// a free function or a small class with one job, and every roll goes through DirectorRandom so
	// a test can script it. docs/director.md says which rules are read from Darkspore.exe and which
	// are approximate.

	// DirectorRandom
	// Where the director's rolls come from.
	class DirectorRandom {
		public:
			virtual ~DirectorRandom() = default;

			// Uniform in [0, 1), as the client's 0xae52a0.
			virtual float Unit() = 0;

			// Uniform in [0, count), as the client's 0x9bce90 and 0xae5480. 0 when count is 0.
			virtual uint32_t Below(uint32_t count) = 0;
	};

	// DefaultDirectorRandom
	// Rolls with a generator of its own. utils::random's is never seeded, so every game would make
	// the same picks; this one is seeded from std::random_device, or from a seed given to replay a run.
	class DefaultDirectorRandom final : public DirectorRandom {
		public:
			DefaultDirectorRandom();
			explicit DefaultDirectorRandom(uint32_t seed);

			float Unit() override;
			uint32_t Below(uint32_t count) override;

			uint32_t GetSeed() const;

		private:
			uint32_t mSeed;
			std::mt19937 mGenerator;
	};

	// ScriptedDirectorRandom
	// Hands out scripted rolls in order, for tests. Unit() and Below() have separate scripts.
	// A Below() value past the end of its range is held to count - 1. Once a script runs out it
	// returns 0, and Overruns() counts how often that happened.
	class ScriptedDirectorRandom final : public DirectorRandom {
		public:
			ScriptedDirectorRandom() = default;
			ScriptedDirectorRandom(std::vector<float> units, std::vector<uint32_t> picks);

			void PushUnit(float value);
			void PushBelow(uint32_t value);

			float Unit() override;
			uint32_t Below(uint32_t count) override;

			size_t UnitsLeft() const;
			size_t PicksLeft() const;
			uint32_t Overruns() const;

		private:
			std::deque<float> mUnits;
			std::deque<uint32_t> mPicks;

			uint32_t mOverruns = 0;
	};

	// DirectorSpawnKind
	// What the director makes of a marker, from its noun. The values are the client's own.
	enum class DirectorSpawnKind : uint32_t {
		None = 0,
		Director = 1,
		A = 2,
		B = 3,
		Horde = 5,
		HordeTrigger = 6,
		Wanderer = 7,
		Spike = 8,
		Boss = 9
	};

	// The director kind of a marker's noun (utils::hash_id of nounDef), as the client's 0x9fa9b0.
	// SpawnPoint_DirectorC and every other noun are None.
	DirectorSpawnKind ClassifySpawnMarker(uint32_t nounHash);

	// The client keeps four sections per level (director + 0x1c + i * 0xe8). 0, 1 and 2 are the
	// level's sections A, B and C, the same numbers as a marker's spawnPointDef sectionType.
	// 3 is the whole level (sectionType 3, "any").
	constexpr size_t directorSectionCount = 4;
	constexpr uint32_t directorLevelSection = 3;

	// The most enemies of one kind a level uses (0x9fc430 keeps 3 minions and 3 specials).
	constexpr size_t directorLevelPoolSize = 3;

	// The most members a group has (the client's groups are Noun*[15]).
	constexpr size_t directorMaxGroupSize = 15;

	// DirectorClassEntry
	// One DirectorClass of a LevelConfig, with its NonPlayerClass challengeValue looked up.
	struct DirectorClassEntry {
		uint32_t noun = 0; // utils::hash_id of mpNoun
		uint32_t minDifficulty = 0;
		uint32_t maxDifficulty = 0;
		bool hordeLegal = false;
		int32_t challenge = 0; // NonPlayerClass challengeValue (+0x24)
	};

	// DirectorEnemy
	// An enemy type the director can spawn.
	struct DirectorEnemy {
		uint32_t noun = 0;
		int32_t challenge = 0;
		bool hordeLegal = false;
	};

	// DirectorSection
	// The enemy types of one section. A type can appear twice (see AllocateSections), which makes
	// it likelier to be picked.
	struct DirectorSection {
		std::vector<DirectorEnemy> minions;
		std::vector<DirectorEnemy> specials;
	};

	// DirectorPool
	// The enemy types of a level: sections 0, 1, 2 (A, B, C) and 3 (the whole level).
	struct DirectorPool {
		std::array<DirectorSection, directorSectionCount> sections;
	};

	// DirectorBucket
	// One SectionConfig entry: how many minion and special types a section gets, for which tier.
	struct DirectorBucket {
		uint32_t numMinions = 0;
		uint32_t numSpecials = 0;
		uint32_t difficulty = 0; // The tier, ceil(difficulty / 4)
		float chance = 0; // 0x9fc430 does not read it
	};

	// The level's enemy types on a first visit, as the client's 0x9fbd10: the entries whose
	// difficulty range holds difficulty, shuffled and cut to 3 minions and 3 specials. Each noun is
	// kept once. The result is section 3 of the pool.
	DirectorSection ChooseLevelPool(const std::vector<DirectorClassEntry>& minionEntries,
		const std::vector<DirectorClassEntry>& specialEntries, uint32_t difficulty, DirectorRandom& random);

	// The game's SectionConfig, read from Data/AssetData_Binary.package.
	std::vector<DirectorBucket> DefaultSectionBuckets();

	// Splits the level's types over sections A, B and C, as the end of the client's 0x9fc430: each
	// takes a different bucket of the difficulty's tier and draws its types from bags shared by the
	// three. Section 3 is levelSection itself.
	DirectorPool AllocateSections(const DirectorSection& levelSection, const std::vector<DirectorBucket>& buckets,
		uint32_t difficulty, DirectorRandom& random);

	// DirectorGroup
	// A group being filled against a challenge budget, as the client's AddToGroup (0x9fba70).
	class DirectorGroup {
		public:
			explicit DirectorGroup(int32_t budget);

			// Picks one of candidates at random and adds it if the group still fits the budget.
			// If it does not fit, that type is taken out of candidates instead. False when nothing
			// was added, including when candidates is empty or the group is full.
			bool Add(std::vector<DirectorEnemy>& candidates, DirectorRandom& random);

			bool IsFull() const;

			// True once the last accepted cost has reached the budget.
			bool IsBudgetSpent() const;

			const std::vector<uint32_t>& GetNouns() const;

			int32_t GetBudget() const;
			int32_t GetChallenge() const;

			float GetCost() const;

		private:
			std::vector<uint32_t> mNouns;

			int32_t mBudget = 0;
			int32_t mChallenge = 0;

			float mCost = 0;
	};

	// Whether the player count leaves room for another agent, as the client's 0x9faa40.
	// playersWithCreature counts players whose creature is alive; agents is how many agents the
	// director has spawned (director + 0x45c).
	bool DirectorAgentAllowed(uint32_t difficulty, uint32_t playersWithCreature, uint32_t agents);

	// A group of minions and specials for one spawn point, as the client's 0x9fd660 (0x9fe270
	// group type 4). agentEntries are the planetConfig agents; agentAllowed is DirectorAgentAllowed.
	std::vector<uint32_t> BuildMixedGroup(const DirectorPool& pool, uint32_t section, int32_t budget,
		uint32_t difficulty, DirectorRandom& random,
		const std::vector<DirectorClassEntry>& agentEntries = {}, bool agentAllowed = false);

	// One minion of the section, as the client's 0x9fe270 group type 1. 0 when the pool is empty.
	uint32_t PickWanderer(const DirectorPool& pool, uint32_t section, DirectorRandom& random);

	// How many wanderers a wander point spawns on its hitIndex-th hit (0 for the first), 0 for none.
	// Approximate, from the director designer's GDC 2012 talk.
	uint32_t RollWanderHit(uint32_t hitIndex, DirectorRandom& random);

	// SpikeIntensity
	enum class SpikeIntensity : uint8_t {
		Hard,
		Medium,
		Skip
	};

	// The next spike, from the last two and whether the players are doing well.
	// Approximate, from the director designer's GDC 2012 talk.
	SpikeIntensity NextSpikeIntensity(SpikeIntensity beforeLast, SpikeIntensity last, bool doingWell);

	// SpikeHistory
	// The last two spikes of a run.
	struct SpikeHistory {
		SpikeIntensity beforeLast = SpikeIntensity::Skip;
		SpikeIntensity last = SpikeIntensity::Medium;

		// Picks the next spike and remembers it.
		SpikeIntensity Advance(bool doingWell);
	};

	// The challenge budget of a spike. Approximate: the numbers are darkspin's fallback.
	int32_t SpikeBudget(uint32_t section, SpikeIntensity intensity, uint32_t difficulty);
}

#endif
