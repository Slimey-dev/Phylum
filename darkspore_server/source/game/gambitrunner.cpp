
// Include
#include "gambitrunner.h"

#include "aicontrol.h"
#include "castpayment.h"
#include "conditionregistry.h"
#include "gambitreach.h"
#include "objectmanager.h"

#include "utils/functions.h"

// Game
namespace Game {
	namespace {
		// The AI casts at the first rank; a pet's script reads its owner for anything stronger.
		constexpr int32_t aiRank = 1;
	}

	GambitRunner::GambitRunner(Object& object, Lua& lua, BestTargetFn bestTarget) :
		mObject(object), mLua(lua), mBestTarget(std::move(bestTarget)) {}

	std::optional<GambitCast> GambitRunner::Choose(const Phase& phase, uint8_t& cursor) const {
		const auto& gambits = phase.GetGambit();
		if (gambits.empty()) {
			return std::nullopt;
		}

		std::optional<GambitCast> tooFar;
		switch (phase.GetType()) {
			case PhaseType::Sequential: {
				if (cursor >= gambits.size()) {
					cursor = 0;
				}
				auto cast = Try(gambits[cursor], tooFar);
				if (!cast) {
					cast = std::move(tooFar);
				}
				if (cast) {
					cursor = static_cast<uint8_t>((cursor + 1) % gambits.size());
				}
				return cast;
			}

			case PhaseType::Random: {
				const auto index = utils::random::get<uint32_t>(0, static_cast<uint32_t>(gambits.size() - 1));
				auto cast = Try(gambits[index], tooFar);
				return cast ? cast : tooFar;
			}

			case PhaseType::PrioritizedList:
			default: {
				// A gambit out of range of an ability that does not pursue gives way to the next one:
				// a Zelem shooter blinks in (range 50) rather than walking up for its 12.5 shot.
				for (const auto& gambit : gambits) {
					if (auto cast = Try(gambit, tooFar)) {
						return cast;
					}
				}
				return tooFar;
			}
		}
	}

	std::optional<GambitCast> GambitRunner::Try(const GambitDefinition& gambit, std::optional<GambitCast>& tooFar) const {
		// The cheap checks first: conditions and costs run scripts.
		const auto ability = mLua.GetAbility(gambit.GetAbility());
		if (!ability || mObject.HasCooldown(ability->GetId())) {
			return std::nullopt;
		}

		const auto descriptors = utils::enum_wrapper { ability->GetDescriptors() };
		if (!descriptors.test(Descriptors::IsBasic) && mObject.GetAttributeValue(AttributeType::Silence) != 0) {
			return std::nullopt;
		}

		auto& conditions = mLua.GetConditions();
		const auto result = conditions.Evaluate(gambit.GetCondition(), gambit.GetConditionProperties(), mObject.GetId());
		if (!result.passed) {
			return std::nullopt;
		}

		GambitCast cast { ability };
		if (result.targetId != 0) {
			cast.target = mObject.GetObjectManager().Get(result.targetId);
		} else if (!result.position) {
			// No target and no point: the gambit fights whatever the AI is fighting. A mob's condition
			// that holds without naming one does too; Distance compares nAgent.GetBestTarget's distance
			// and returns only true, and with no target Zelem hybrids shot along their facing and swung
			// at nothing at their own feet. A hero's summon keeps going without one, as before: its
			// FollowOwner and Teleport (DistanceFromOwner) work from the owner, not at an enemy.
			const bool registered = conditions.Contains(gambit.GetCondition());
			if (!registered || !IsHeroSummon(mObject)) {
				cast.target = mBestTarget();
			}
			// A gambit without a condition has nothing else to aim at.
			if (!cast.target && !registered) {
				return std::nullopt;
			}
		}

		if (result.position) {
			cast.position = *result.position;
		} else {
			cast.position = cast.target ? cast.target->GetPosition() : mObject.GetPosition();
		}

		if (!CastPayment(mObject, *ability, ability->GetId(), aiRank).CanPay()) {
			return std::nullopt;
		}

		// Locomotion, not speed: a pet's speed reads 0 here (its MovementSpeedBuff is 0, as Darkspin
		// sends it too) though the client walks it.
		const bool canWalk = mObject.HasLocomotionData();
		cast.inReach = CanReach(*ability, cast.target);
		switch (GambitReach::Decide(cast.inReach, ability->ShouldPursue(), canWalk)) {
			case GambitReach::Answer::Cast:
				return cast;

			case GambitReach::Answer::Pursue:
				// The agent walks up to the target first (AI::Pursue).
				cast.approach = GambitCast::Approach::Pursue;
				return cast;

			case GambitReach::Answer::TooFar:
			default:
				// Kept in case nothing else goes; a turret cannot walk up at all.
				if (!tooFar && canWalk) {
					cast.approach = GambitCast::Approach::TooFar;
					tooFar = std::move(cast);
				}
				return std::nullopt;
		}
	}

	float GambitRunner::Range(const GambitCast& cast) const {
		return cast.ability ? cast.ability->GetRange(aiRank) : 0.f;
	}

	float GambitRunner::ReachDistance(const GambitCast& cast) const {
		// Centre to centre, a little inside the range, so the agent is in reach where it stops.
		constexpr float margin = 0.1f;
		const float range = Range(cast);
		const float radii = mObject.GetFootprintRadius() + (cast.target ? cast.target->GetFootprintRadius() : 0.f);
		return std::max(range - margin, 0.f) + radii;
	}

	bool GambitRunner::CanReach(const Ability& ability, const ObjectPtr& target) const {
		if (!target) {
			return true;
		}

		const float range = ability.GetRange(aiRank);
		if (range <= 0) {
			return true;
		}

		// Edge to edge, as nGameObject.GetObjectDistance measures.
		const float gap = glm::distance(mObject.GetPosition(), target->GetPosition()) - mObject.GetFootprintRadius() - target->GetFootprintRadius();
		return gap <= range;
	}
}
