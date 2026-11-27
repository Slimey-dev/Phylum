
#ifndef _GAME_GAMBITRUNNER_HEADER
#define _GAME_GAMBITRUNNER_HEADER

// Include
#include "object.h"

#include <functional>
#include <optional>

// Game
namespace Game {
	// GambitCast
	// What the AI casts, at what and where.
	struct GambitCast {
		// Why the agent walks before it casts, when its target is out of the ability's range.
		enum class Approach : uint8_t {
			// In reach: it casts now.
			None,
			// The ability pursues (shouldPursue): the client's admission answers 3, "pursue"
			// (Darkspore.exe 0x9e0660 reads ability +0x151), and the agent walks up and casts.
			Pursue,
			// No gambit could go, and this one only because its ability does not pursue and its target
			// is out of range: the client's admission refuses that cast (-0x270c). The client's tree then
			// follows the best target (node [6], 0xa20f10: targetTooFar, else nBehavior_Follow), which
			// AI::Pursue stands in for. Approximate: the server code that raised that node's flag 8 is lost.
			TooFar
		};

		AbilityPtr ability;
		ObjectPtr target;
		glm::vec3 position {};
		// Within the ability's range of the target now; if not, the agent walks there first.
		bool inReach = true;
		Approach approach = Approach::None;
	};

	// GambitRunner
	// Picks what an AI phase does now. A prioritized list (every phase in the game's data) takes the
	// first gambit that can go: its ability is ready and paid for, its condition holds, and the
	// target is within the ability's range, or the ability pursues and the agent can walk there (a
	// turret cannot). A condition gives its own target (Closest, SpriteHealTargeting); one that holds
	// without naming any (Distance, which measures to nAgent.GetBestTarget) and a gambit without a
	// condition (TurretTrap) fight the AI's best target. A gambit without a condition needs that target,
	// or does nothing. A sequential phase tries only its next gambit, a random one any one. When nothing
	// can go only because targets are out of range, the first such gambit comes back as TooFar.
	class GambitRunner {
		public:
			using BestTargetFn = std::function<ObjectPtr()>;

			GambitRunner(Object& object, Lua& lua, BestTargetFn bestTarget);

			// cursor keeps a sequential phase's place from one call to the next.
			std::optional<GambitCast> Choose(const Phase& phase, uint8_t& cursor) const;

			// The cast's range at the AI's rank (edge to edge).
			float Range(const GambitCast& cast) const;

			// How near (centre to centre) the agent must come to be in the cast's reach.
			float ReachDistance(const GambitCast& cast) const;

		private:
			// tooFar keeps the first gambit refused only for its range, for when nothing else goes.
			std::optional<GambitCast> Try(const GambitDefinition& gambit, std::optional<GambitCast>& tooFar) const;
			bool CanReach(const Ability& ability, const ObjectPtr& target) const;

		private:
			Object& mObject;
			Lua& mLua;
			BestTargetFn mBestTarget;
	};
}

#endif
