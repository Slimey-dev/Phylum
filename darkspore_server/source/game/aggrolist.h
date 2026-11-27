#ifndef _GAME_AGGROLIST_HEADER
#define _GAME_AGGROLIST_HEADER

// Include
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// Game
namespace Game {
	// AggroFlags
	// Why threat was added: the third argument of the client's AddAggro (Darkspore.exe 0x9e4640).
	namespace AggroFlags {
		enum : uint32_t {
			// nGameObject.AddAggroForObject (0xa02c0e): threat only, no reaction.
			Script = 1 << 0,
			// Aggro_Trigger (0xa22620) and Spawn Aggro (0x9febc0): a first entry is a first aggro.
			Perceived = 1 << 1,
			// Approximate: the server's damage threat. No client caller passes 4, but it reacts as 2
			// does (test al,6 at 0x9e471b).
			Damage = 1 << 2,
			// An alert from an ally: a first entry reacts late (see HasAggroed).
			Alerted = 1 << 3
		};
	}

	// AggroStimulus
	// The reaction a change to the list asks the brain for.
	enum class AggroStimulus : uint8_t {
		None = 0,
		// Stimulus 0x20, posted by the first entry ever for flags 2 or 4 (0x9e47d7).
		FirstAggro,
		// Stimulus 0x200, posted by the first entry ever for flag 8 (0x9e474a) or by AlertObject (0x9e48ef).
		FirstAlert,
		// Stimulus 0x80. Approximate: nothing in the client posts it. Posted here when a list that
		// emptied after a fight gets a new entry.
		SubsequentAggro
	};

	// AggroEntry
	struct AggroEntry {
		uint32_t id = 0;
		float threat = 0;
	};

	// AggroCandidate
	// What GetBestTarget asks about an entry's object (0x9e3e50).
	struct AggroCandidate {
		// It has an agent blackboard: heroes and NPCs do, scenery does not.
		bool hasBlackboard = true;
		bool targetable = true;
		bool stealthed = false;
		// It has combatant data (0x9d9540).
		bool combatant = true;
		// Intangible (attribute 29) above 0 (0x9d9540).
		bool intangible = false;
	};

	// AggroList
	// An NPC's threat list, as the client's agent blackboard keeps it (+0x228, {id, threat} pairs).
	// The client adds to an entry that is listed and appends a new one, and GetBestTarget takes the
	// first entry that may be fought. It never sorts, and nothing in the client says how the server
	// ranked threat. INFERRED: Entangle's taunt (ability_entangle.lua, the target's threat minus the
	// caster's plus 5) only works if the highest threat wins, so the list is kept in threat order,
	// highest first, and entries with the same threat stay in the order they were listed (the
	// client's own order).
	class AggroList {
		public:
			// AlertObject's threat (0x9e4870, 0x40a00000), also what Aggro_Trigger and Spawn Aggro add.
			static constexpr float alertThreat = 5.f;
			// AddAggro never adds less (0x9e46cd), so a negative change (taunt_end's -100) adds this.
			static constexpr float minimumThreat = 0.01f;
			// How long the first-aggro and alert stimuli last (0xfd0fc8).
			static constexpr float stimulusSeconds = 10.f;

			// The AI definition's aggroType (+0x78, 0x9e3b30): 2 never aggroes, only 0 can be alerted.
			// The exported XML once held the passiveAbility name pointer there instead, so any other
			// value reads as 0 (every real value is 0 or 2).
			static uint32_t AggroTypeFromData(uint32_t value);

			explicit AggroList(uint32_t aggroType = 0);

			uint32_t GetAggroType() const;

			// AddAggro (0x9e4640). increase and decrease are the source's AggroIncrease and
			// AggroDecrease (attributes 86 and 87). roll is uniform in [0, 1), for an alert's delay.
			AggroStimulus Add(uint32_t id, float amount, uint32_t flags, float increase, float decrease, uint64_t nowMs, float roll);

			// AlertObject (0x9e4870): 5 threat for a source that is not listed yet. It reacts at once,
			// unless given a roll in [0, 1): then a first entry reacts after the same delay an
			// alerted AddAggro gets (INFERRED, for the allies an agent alerts on its first aggro).
			AggroStimulus Alert(uint32_t id, uint64_t nowMs = 0, float roll = -1.f);

			// GetAggroForObject (0xa02c20 -> 0x9e3e10): 0 for a source that is not listed.
			float Get(uint32_t id) const;
			bool Contains(uint32_t id) const;
			bool IsEmpty() const;

			// The entries, highest threat first.
			std::vector<AggroEntry> GetEntries() const;
			// GetTargetsOnAggroList (0xa07f50): the ids in list order.
			std::vector<uint32_t> GetIds() const;

			// Drops the entries the predicate picks and answers their ids. The client never drops an
			// entry; the server drops those whose object is gone or dead (INFERRED).
			std::vector<uint32_t> RemoveIf(const std::function<bool(uint32_t)>& predicate);
			void Clear();

			// GetBestTarget (0x9e3e50): the first entry whose object resolves (lookup answers it), is
			// seen (the agent detects stealth, or it has no blackboard, or it is not stealthed), has a
			// blackboard that makes it targetable, is a combatant and is not intangible. Health is not
			// checked. 0 when none passes.
			uint32_t BestTarget(const std::function<std::optional<AggroCandidate>(uint32_t)>& lookup, bool detectsStealth) const;

			// HasAggroed (0x9e3c70): the first entry was ever listed, and an alert's delay is over.
			bool HasAggroed(uint64_t nowMs) const;
			// Whether anything was ever listed (+0x555), however late an alert reacts.
			bool IsLatched() const;

			// IgnoreFirstAggro (0x9e3cc0): as if it had aggroed, so no first reaction comes. The brain
			// clears the stimuli (0x2a0).
			void IgnoreFirstAggro();

		private:
			struct Entry {
				uint32_t id = 0;
				float threat = 0;
				// When it was listed: entries with the same threat keep that order.
				uint64_t order = 0;
			};

			std::vector<Entry>::iterator Find(uint32_t id);
			std::vector<Entry>::const_iterator Find(uint32_t id) const;
			void Append(uint32_t id, float threat);
			// Moves the entry up past those with less threat.
			void Rise(std::vector<Entry>::iterator entry);
			// What a new entry asks for on a list that emptied after a fight (see SubsequentAggro).
			AggroStimulus Again() const;

		private:
			std::vector<Entry> mEntries;

			uint64_t mNextOrder = 0;
			// The time before which an alerted agent has not aggroed yet (bt+0x720).
			uint64_t mAggroedAtMs = 0;
			uint32_t mAggroType = 0;

			// Something was ever listed (+0x555).
			bool mLatched = false;
			// Something was listed and the list has emptied since.
			bool mEmptied = false;
	};
}

#endif
