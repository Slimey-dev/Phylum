
#ifndef _GAME_AITARGETING_HEADER
#define _GAME_AITARGETING_HEADER

// Include
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Game
namespace Game {
	// TargetCandidate
	// A creature the AI could fight, as it is at this moment.
	struct TargetCandidate {
		uint32_t id = 0;
		glm::vec3 position {};
		uint8_t team = 0;
		bool alive = true;
		bool targetable = true;
		bool stealthed = false;
	};

	// A hero summon's targets, best first: live, targetable candidates on another team within the
	// radius, nearest first, a tie going to the lower id. A stealthed one only counts for an agent
	// that detects stealth. The client's nAgent.GetBestTarget takes the first entry of its aggro list
	// that passes the same checks (Darkspore.exe 0x9e3e50); a mob keeps that list (AggroList), while
	// a summon still lets the hostiles in aggro range stand in for it. The AI asks again every time
	// it thinks, so a target that died or went away is dropped.
	std::vector<uint32_t> OrderHostiles(const glm::vec3& from, uint8_t team, float radius, bool detectsStealth,
		const std::vector<TargetCandidate>& candidates, uint32_t selfId);

	// The first of OrderHostiles, or 0 when there is none.
	uint32_t PickNearestHostile(const glm::vec3& from, uint8_t team, float radius, const std::vector<TargetCandidate>& candidates, uint32_t selfId);

	// The radius of the circle an NPC perceives (0x9e4040): the larger of its NonPlayerClass's
	// aggroRange (+0x38) and alertRange (+0x3c), or 20 when it has no NPC class.
	float PerceptionRadius(bool hasClass, float aggroRange, float alertRange);

	// nAgent.InPerceptionCircle (0xa0aea0): whether a point, widened by its radius, touches a circle.
	// Strict: radius > |point - center| - pointRadius.
	bool TouchesCircle(const glm::vec3& center, float circle, const glm::vec3& point, float pointRadius);

	// AggroEntrant
	// A creature that came near an NPC, for its aggro trigger.
	struct AggroEntrant {
		uint8_t team = 0;
		// It has combatant data and is not intangible (0x9d9540), and its health is above 0 (0x9d9570).
		bool combatant = true;
		bool intangible = false;
		bool alive = true;
		// A hero, or something a hero made (IsHero, IsHeroSummon).
		bool heroSide = true;
	};

	// Whether the NPC's aggro trigger takes this entrant (Aggro_Trigger 0xa22620 -> 0x9d95c0): a live,
	// tangible combatant on another team. Approximate: only heroes and their summons count, so the
	// level's friendly creatures (team 1 as the heroes are) start no fight.
	bool IsAggroEntrant(uint8_t team, const AggroEntrant& entrant);
}

#endif
