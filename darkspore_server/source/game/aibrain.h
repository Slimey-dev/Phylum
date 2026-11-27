#ifndef _GAME_AIBRAIN_HEADER
#define _GAME_AIBRAIN_HEADER

// Include
#include <array>
#include <cstdint>
#include <string>

// Game
namespace Game {
	// AIStimulus
	// The stimuli an agent's behavior tree reacts to, as the scripts name them (nStimulusFlags,
	// lua/GlobalDefinitions.lua). FirstAlert has no name there; AddAggro and AlertObject post it.
	namespace AIStimulus {
		enum : uint32_t {
			UseAbility = 1 << 0,
			UseModifier = 1 << 1,
			AbilityOrdersChanged = 1 << 2,
			TookDamage = 1 << 3,
			MoveToRange = 1 << 4,
			FirstAggro = 1 << 5,
			AllyOfSameTypeDied = 1 << 6,
			SubsequentAggro = 1 << 7,
			AllyDied = 1 << 8,
			FirstAlert = 1 << 9,

			// What IgnoreFirstAggro clears (0x9e3cc0).
			AggroReactions = FirstAggro | SubsequentAggro | FirstAlert
		};
	}

	// AIStimuli
	// An agent's timed stimuli: eight slots that each hold a mask until its time runs out (bt+0x10).
	class AIStimuli {
		public:
			static constexpr size_t slotCount = 8;

			// 0xa64690: a post replaces the slot with the same mask, else takes a free slot, else the
			// slot with the least time left.
			void Post(uint32_t mask, float seconds);
			// 0xa64780: frees every slot that holds any of these.
			void Clear(uint32_t mask);
			// 0xa64810, once per think: time passes, and a slot whose time is up is freed.
			void Decay(float seconds);

			// 0xa64540: whether any slot holds any of these.
			bool IsActive(uint32_t mask) const;
			// Every mask that is held.
			uint32_t Active() const;

		private:
			struct Slot {
				uint32_t mask = 0;
				float seconds = 0;
			};

			std::array<Slot, slotCount> mSlots {};
	};

	// BrainNode
	// The child of the client's creature tree (0x011814e8, a priority selector run by 0xa329b0)
	// that has the agent this think, highest priority first.
	enum class BrainNode : uint8_t {
		// [1] HP 0: nBehavior_Death (0xa21160).
		Death = 0,
		// [2] A running ability that needs its agent keeps it (0xa20e70 -> 0x9dee30): one action at a time.
		HoldCast,
		// Approximate, server-only: the reactions to the aggro stimuli (see BrainProfile).
		FirstAggroCast,
		FirstAlertCast,
		SubsequentAggroCast,
		// [4], [7]-[9] In combat: the gambits, and the combat idles between them.
		Combat,
		// [10] preAggroIdle until it first aggroes, passiveIdle after (0xa21e20, 0xa214d0).
		Idle,
		// [11] nBehavior_Wander when no idle is named.
		Wander,
		// Anything but a creature has the short tree (0x01181570, from 0x9d6860), whose last child does
		// nothing: no idles, no wander and no nBehavior_Death.
		Rest
	};

	// BrainState
	// What the agent is like at this think.
	struct BrainState {
		bool dead = false;
		// One of its abilities that needs the agent still runs.
		bool castRunning = false;
		// AggroList::HasAggroed.
		bool hasAggroed = false;
		// The aggro list has a target it may fight (AggroList::BestTarget).
		bool hasTarget = false;
		// AIDefinition.alwaysRunAI: its gambits run before it has a target (a buffer).
		bool alwaysRun = false;
		// IdleFor names a behavior.
		bool hasIdle = false;
		// Its noun is a creature (nounType 0x06c27d00), which gets the creature tree.
		bool creature = true;
		// AIStimuli::Active.
		uint32_t stimuli = 0;
	};

	// BrainProfile
	// The parts of an AI definition (AIDefinition, 0xf8bca0) the brain picks from.
	struct BrainProfile {
		std::string preAggroIdle;
		std::string secondaryPreAggroIdle;
		std::string combatIdle;
		std::string passiveIdle;

		uint32_t firstAggroAbility = 0;
		uint32_t secondaryFirstAggroAbility = 0;
		uint32_t firstAlertAbility = 0;
		uint32_t subsequentAggroAbility = 0;

		float useSecondaryStart = 0;
	};

	// The behaviors the brain falls back on, which every creature has (0xa213e0, 0xa21610, 0xa21160).
	constexpr const char* combatIdleBehavior = "nBehavior_CombatIdle";
	constexpr const char* wanderBehavior = "nBehavior_Wander";
	constexpr const char* deathBehavior = "nBehavior_Death";
	// The pre-aggro idle that hides its agent until it reveals (behaviors/Behavior_Invisible.lua).
	constexpr const char* invisibleBehavior = "nBehavior_Invisible";

	// The selector: the first child, in priority order, that wants the agent.
	// In combat means a target and HasAggroed, so an alerted agent waits out its delay first
	// (INFERRED; the delay is only read by HasAggroed). A reaction needs a target to face.
	BrainNode ChooseNode(const BrainState& state);

	// Brain init (0xa22a50, at 0xa22ba1): a secondary start when a roll in [0, 1) is below
	// useSecondaryStart.
	bool RollSecondaryStart(float useSecondaryStart, float roll);

	// The idle node's behavior (0xa214d0): preAggroIdle until it has aggroed, or preAggroIdle2 on a
	// secondary start (0x9e3d00), and passiveIdle otherwise. Empty when the node does not score
	// (0xa21e20), and the agent wanders.
	std::string IdleFor(const BrainProfile& profile, bool secondaryStart, bool hasAggroed);

	// The combat idle: combatIdle when the definition names one (node [8], cooldown 0 on 1-1), else
	// nBehavior_CombatIdle (node [9]). combatIdle2 needs its condition and is not run (see docs/ai.md).
	std::string CombatIdleFor(const BrainProfile& profile);

	// What a reaction casts. INFERRED: the client reads none of these. firstAggroAbility2 goes with
	// the secondary start: every one of the 35 definitions that name it names FirstAggro_FaceTarget
	// with a visible preAggroIdle2 (nBehavior_Wander), while firstAggroAbility beams or digs an
	// invisible one in. An ability set by OverrideFirstAggro (blackboard +0x584) wins for the first
	// aggro and the first alert, as ability_shadowteleport.lua sets it right after AlertObject.
	uint32_t FirstAggroAbilityFor(const BrainProfile& profile, bool secondaryStart, uint32_t overrideAbility);
	uint32_t FirstAlertAbilityFor(const BrainProfile& profile, uint32_t overrideAbility);
}

#endif
