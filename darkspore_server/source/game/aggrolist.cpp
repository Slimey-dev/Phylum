// Include
#include "aggrolist.h"

#include <algorithm>

// Game
namespace Game {
	namespace {
		// aggroType 2 never aggroes (0x9e4640 returns at once).
		constexpr uint32_t neverAggroes = 2;

		// An alerted agent reacts 0.5 to 1.25 seconds late (0x9e4754-0x9e4769).
		constexpr float alertDelayBase = 0.5f;
		constexpr float alertDelaySpread = 0.75f;
	}

	uint32_t AggroList::AggroTypeFromData(uint32_t value) {
		return value == neverAggroes ? neverAggroes : 0;
	}

	AggroList::AggroList(uint32_t aggroType) : mAggroType(AggroTypeFromData(aggroType)) {}

	uint32_t AggroList::GetAggroType() const {
		return mAggroType;
	}

	AggroStimulus AggroList::Add(uint32_t id, float amount, uint32_t flags, float increase, float decrease, uint64_t nowMs, float roll) {
		if (mAggroType == neverAggroes) {
			return AggroStimulus::None;
		}

		// The source's attributes scale it, and the result is clamped, so a negative change adds a little.
		amount = amount * (1.f + increase);
		amount = amount - decrease * amount;
		amount = std::max(minimumThreat, amount);

		if (auto entry = Find(id); entry != mEntries.end()) {
			entry->threat += amount;
			Rise(entry);
			return AggroStimulus::None;
		}

		// Only the first entry ever reacts, and how depends on why it came.
		auto stimulus = Again();
		if (mEntries.empty() && !mLatched) {
			if (flags & (AggroFlags::Perceived | AggroFlags::Damage)) {
				stimulus = AggroStimulus::FirstAggro;
			} else if (flags & AggroFlags::Alerted) {
				stimulus = AggroStimulus::FirstAlert;
				const float delay = (roll * alertDelaySpread + alertDelayBase) * 1000.f;
				mAggroedAtMs = nowMs + static_cast<uint64_t>(delay);
			}
		}

		mLatched = true;
		Append(id, amount);
		return stimulus;
	}

	AggroStimulus AggroList::Alert(uint32_t id, uint64_t nowMs, float roll) {
		// Only an agent that aggroes normally can be alerted, and only by a source it does not list.
		if (mAggroType != 0 || Contains(id)) {
			return AggroStimulus::None;
		}

		auto stimulus = Again();
		if (mEntries.empty() && !mLatched) {
			stimulus = AggroStimulus::FirstAlert;
			if (roll >= 0.f) {
				const float delay = (roll * alertDelaySpread + alertDelayBase) * 1000.f;
				mAggroedAtMs = nowMs + static_cast<uint64_t>(delay);
			}
		}

		mLatched = true;
		Append(id, alertThreat);
		return stimulus;
	}

	float AggroList::Get(uint32_t id) const {
		const auto entry = Find(id);
		return entry != mEntries.end() ? entry->threat : 0.f;
	}

	bool AggroList::Contains(uint32_t id) const {
		return Find(id) != mEntries.end();
	}

	bool AggroList::IsEmpty() const {
		return mEntries.empty();
	}

	std::vector<AggroEntry> AggroList::GetEntries() const {
		std::vector<AggroEntry> entries;
		entries.reserve(mEntries.size());
		for (const auto& entry : mEntries) {
			entries.push_back({ entry.id, entry.threat });
		}
		return entries;
	}

	std::vector<uint32_t> AggroList::GetIds() const {
		std::vector<uint32_t> ids;
		ids.reserve(mEntries.size());
		for (const auto& entry : mEntries) {
			ids.push_back(entry.id);
		}
		return ids;
	}

	std::vector<uint32_t> AggroList::RemoveIf(const std::function<bool(uint32_t)>& predicate) {
		std::vector<uint32_t> removed;
		std::erase_if(mEntries, [&](const Entry& entry) {
			if (!predicate(entry.id)) {
				return false;
			}
			removed.push_back(entry.id);
			return true;
		});
		if (!removed.empty() && mEntries.empty()) {
			mEmptied = true;
		}
		return removed;
	}

	void AggroList::Clear() {
		if (!mEntries.empty()) {
			mEmptied = true;
		}
		mEntries.clear();
	}

	uint32_t AggroList::BestTarget(const std::function<std::optional<AggroCandidate>(uint32_t)>& lookup, bool detectsStealth) const {
		for (const auto& entry : mEntries) {
			const auto candidate = lookup(entry.id);
			if (!candidate) {
				continue;
			}
			// A stealthed one is seen only by an agent that detects stealth (attribute 0x22).
			if (!detectsStealth && candidate->hasBlackboard && candidate->stealthed) {
				continue;
			}
			if (!candidate->hasBlackboard || !candidate->targetable) {
				continue;
			}
			if (!candidate->combatant || candidate->intangible) {
				continue;
			}
			return entry.id;
		}
		return 0;
	}

	bool AggroList::HasAggroed(uint64_t nowMs) const {
		return mLatched && nowMs >= mAggroedAtMs;
	}

	bool AggroList::IsLatched() const {
		return mLatched;
	}

	void AggroList::IgnoreFirstAggro() {
		mLatched = true;
	}

	std::vector<AggroList::Entry>::iterator AggroList::Find(uint32_t id) {
		return std::find_if(mEntries.begin(), mEntries.end(), [id](const Entry& entry) { return entry.id == id; });
	}

	std::vector<AggroList::Entry>::const_iterator AggroList::Find(uint32_t id) const {
		return std::find_if(mEntries.begin(), mEntries.end(), [id](const Entry& entry) { return entry.id == id; });
	}

	void AggroList::Append(uint32_t id, float threat) {
		mEntries.push_back({ id, threat, mNextOrder++ });
		Rise(std::prev(mEntries.end()));
	}

	void AggroList::Rise(std::vector<Entry>::iterator entry) {
		// Highest threat first; on a tie the one listed first stays first.
		const auto ahead = [](const Entry& a, const Entry& b) {
			return a.threat > b.threat || (a.threat == b.threat && a.order < b.order);
		};
		while (entry != mEntries.begin() && ahead(*entry, *std::prev(entry))) {
			std::iter_swap(entry, std::prev(entry));
			--entry;
		}
	}

	AggroStimulus AggroList::Again() const {
		// Approximate: the client never posts SubsequentAggro (0x80), so a list that emptied after a
		// fight asks for it when someone is listed again.
		return mEntries.empty() && mLatched && mEmptied ? AggroStimulus::SubsequentAggro : AggroStimulus::None;
	}
}
