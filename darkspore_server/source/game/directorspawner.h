
#ifndef _GAME_DIRECTORSPAWNER_HEADER
#define _GAME_DIRECTORSPAWNER_HEADER

// Include
#include "director.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Game
namespace Game {
	// When the director's spawn points fire. The client keeps no scheduler (Director::Init runs with
	// populate false at 0x4e74b6, and nothing reads the spawn records' activation), so this part is
	// approximate: it follows the director designer's GDC 2012 talk, with darkspin's fallback
	// distances (github.com/darkspinnet/darkspin, MIT). See docs/director.md.

	// A wander point is hit when a hero comes within this distance of it. A point a hero is already
	// that close to when it is added (the start of the level) is not hit until the hero leaves and
	// comes back, so nothing spawns on top of the players.
	constexpr float directorWanderHitRadius = 25.f;

	// A spike fires once, the first time a hero comes this close to any of its points.
	constexpr float directorSpikeTriggerRadius = 25.f;

	// A hero who moved further than this since the last update did not walk there: a teleporter, a
	// swap or a revive put it down. It does not count as arriving, or every point around where it
	// landed would fire at once, next to it. Updates are a quarter second apart.
	constexpr float directorArrivalJump = 12.f;

	// How far a spawned creature is set from its point. The client scatters by up to 10 m along +x
	// and +y and projects onto the navmesh (0x9fb180); the server has no navmesh, so it stays close.
	constexpr float directorSpawnScatter = 2.f;

	// DirectorSpawnRequest
	// A creature the director wants spawned.
	struct DirectorSpawnRequest {
		uint32_t noun = 0;
		glm::vec3 position {};
		glm::quat orientation { 1.f, 0.f, 0.f, 0.f };
		// What made it, for the trace: "wander" or "spike".
		const char* source = "";
		// The point's markerset (spikes) or index (wanders), for the trace.
		std::string point;
	};

	// DirectorSpawner
	// The spawn points of one run, and which of them fire as the heroes move.
	class DirectorSpawner {
		public:
			struct SpikePoint {
				glm::vec3 position {};
				glm::quat orientation { 1.f, 0.f, 0.f, 0.f };
			};

			// A SpawnPoint_DirectorWanderer, in section 0, 1 or 2 (3 for any).
			void AddWanderPoint(const glm::vec3& position, uint32_t section);

			// The SpawnPoint_DirectorSpike markers of one markerset, which spawn as one group.
			void AddSpikeSet(std::string name, std::vector<SpikePoint> points, uint32_t section);

			// Checks the points against where the heroes are, and returns the creatures to spawn.
			// doingWell is the talk's "doing well" for the next spike. heroes keeps the same order from
			// one update to the next; a hero whose index changed counts as having jumped.
			std::vector<DirectorSpawnRequest> Update(const std::vector<glm::vec3>& heroes, bool doingWell,
				const DirectorPool& pool, uint32_t difficulty, DirectorRandom& random);

			size_t GetWanderPointCount() const;
			size_t GetSpikeSetCount() const;
			const SpikeHistory& GetSpikeHistory() const;

		private:
			struct WanderPoint {
				glm::vec3 position {};
				uint32_t section = directorLevelSection;
				uint32_t hits = 0;
				bool spawned = false;
				// Whether a hero was within the hit radius at the last update.
				bool heroNear = false;
				// Not hit by an update yet: a hero already near it is not counted as arriving.
				bool seen = false;
			};

			struct SpikeSet {
				std::string name;
				std::vector<SpikePoint> points;
				uint32_t section = directorLevelSection;
				bool fired = false;
				bool heroNear = false;
				bool seen = false;
			};

			// The heroes now, and those of them that walked here since the last update.
			struct Heroes {
				const std::vector<glm::vec3>& all;
				const std::vector<glm::vec3>& walked;
			};

			void UpdateWanderPoint(WanderPoint& point, size_t index, const Heroes& heroes,
				const DirectorPool& pool, DirectorRandom& random, std::vector<DirectorSpawnRequest>& spawns);
			void UpdateSpikeSet(SpikeSet& set, const Heroes& heroes, bool doingWell,
				const DirectorPool& pool, uint32_t difficulty, DirectorRandom& random, std::vector<DirectorSpawnRequest>& spawns);

			std::vector<glm::vec3> mLastHeroes;
			std::vector<WanderPoint> mWanderPoints;
			std::vector<SpikeSet> mSpikeSets;
			SpikeHistory mSpikeHistory;
	};

	// The section most of a set's markers name. A section of 3 ("any") counts only when no marker
	// names A, B or C.
	uint32_t MostCommonSection(const std::vector<uint32_t>& sections);
}

#endif
