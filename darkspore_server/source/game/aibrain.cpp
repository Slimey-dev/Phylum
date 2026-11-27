// Include
#include "aibrain.h"

#include <algorithm>

// Game
namespace Game {
	// AIStimuli
	void AIStimuli::Post(uint32_t mask, float seconds) {
		if (mask == 0) {
			return;
		}

		auto slot = std::find_if(mSlots.begin(), mSlots.end(), [mask](const Slot& slot) { return slot.mask == mask; });
		if (slot == mSlots.end()) {
			slot = std::find_if(mSlots.begin(), mSlots.end(), [](const Slot& slot) { return slot.mask == 0; });
		}
		if (slot == mSlots.end()) {
			slot = std::min_element(mSlots.begin(), mSlots.end(), [](const Slot& a, const Slot& b) { return a.seconds < b.seconds; });
		}
		*slot = { mask, seconds };
	}

	void AIStimuli::Clear(uint32_t mask) {
		for (auto& slot : mSlots) {
			if (slot.mask & mask) {
				slot = {};
			}
		}
	}

	void AIStimuli::Decay(float seconds) {
		for (auto& slot : mSlots) {
			if (slot.mask == 0) {
				continue;
			}
			slot.seconds -= seconds;
			if (slot.seconds <= 0) {
				slot = {};
			}
		}
	}

	bool AIStimuli::IsActive(uint32_t mask) const {
		return (Active() & mask) != 0;
	}

	uint32_t AIStimuli::Active() const {
		uint32_t mask = 0;
		for (const auto& slot : mSlots) {
			mask |= slot.mask;
		}
		return mask;
	}

	// Brain
	BrainNode ChooseNode(const BrainState& state) {
		if (state.dead) {
			return BrainNode::Death;
		}
		if (state.castRunning) {
			return BrainNode::HoldCast;
		}

		// A reaction faces its target, so it waits for one. The first alert also waits for HasAggroed.
		if (state.hasTarget) {
			if (state.stimuli & AIStimulus::FirstAggro) {
				return BrainNode::FirstAggroCast;
			}
			if ((state.stimuli & AIStimulus::FirstAlert) && state.hasAggroed) {
				return BrainNode::FirstAlertCast;
			}
			if (state.stimuli & AIStimulus::SubsequentAggro) {
				return BrainNode::SubsequentAggroCast;
			}
		}

		if ((state.hasTarget && state.hasAggroed) || state.alwaysRun) {
			return BrainNode::Combat;
		}
		if (!state.creature) {
			return BrainNode::Rest;
		}
		return state.hasIdle ? BrainNode::Idle : BrainNode::Wander;
	}

	bool RollSecondaryStart(float useSecondaryStart, float roll) {
		return useSecondaryStart > 0 && roll < useSecondaryStart;
	}

	std::string IdleFor(const BrainProfile& profile, bool secondaryStart, bool hasAggroed) {
		if (!hasAggroed) {
			const auto& preAggro = secondaryStart ? profile.secondaryPreAggroIdle : profile.preAggroIdle;
			if (!preAggro.empty()) {
				return preAggro;
			}
		}
		return profile.passiveIdle;
	}

	std::string CombatIdleFor(const BrainProfile& profile) {
		return profile.combatIdle.empty() ? combatIdleBehavior : profile.combatIdle;
	}

	uint32_t FirstAggroAbilityFor(const BrainProfile& profile, bool secondaryStart, uint32_t overrideAbility) {
		if (overrideAbility != 0) {
			return overrideAbility;
		}
		return secondaryStart ? profile.secondaryFirstAggroAbility : profile.firstAggroAbility;
	}

	uint32_t FirstAlertAbilityFor(const BrainProfile& profile, uint32_t overrideAbility) {
		return overrideAbility != 0 ? overrideAbility : profile.firstAlertAbility;
	}
}
