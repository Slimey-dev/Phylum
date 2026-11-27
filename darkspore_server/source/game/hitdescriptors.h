
#ifndef _GAME_HITDESCRIPTORS_HEADER
#define _GAME_HITDESCRIPTORS_HEADER

// Include
#include "attributes.h"

#include <cstdint>
#include <vector>

// Game
namespace Game {
	enum class CharacterAnimationType : uint8_t;

	// HitDescriptors
	// How the client describes a hit that landed (nHitDescriptors, GlobalDefinitions.lua). It keeps the
	// last one on the victim (+0x2cc, then +0x28), which nGameObject.GetHitDescriptors reads (0xa03ad0):
	// on a corpse, the hit that killed it. Condition_ShouldRepair and Behavior_Death read Critical there,
	// and SetAnimationStateToDeath picks the death from it (0xa018d0).
	// Approximate: where the client builds the bits is not traced; they follow their names.
	class HitDescriptors {
		public:
			static constexpr uint32_t Critical = 1;
			static constexpr uint32_t SourcePhysical = 2;
			static constexpr uint32_t SourceEnergy = 4;
			// TypeTechnology, then Spacetime, Life, Elements and Supernatural, in nDamageTypes order.
			static constexpr uint32_t TypeTechnology = 8;
			static constexpr uint32_t TypeSpacetime = 16;
			static constexpr uint32_t TypeLife = 32;
			static constexpr uint32_t TypeElements = 64;
			static constexpr uint32_t TypeSupernatural = 128;
			static constexpr uint32_t AoE = 256;
			static constexpr uint32_t DoT = 512;
			static constexpr uint32_t Knockback = 1024;

			// The bits of one hit. A Generic hit has no type bit.
			static uint32_t Of(bool critical, DamageSource source, DamageType type, Descriptors descriptors, bool knockback);

			// The deaths to try for a killing hit, best first (0xa018d0). A hit that was not critical
			// gives MeleeDeath. A critical one gives its family's death: Melee for a physical hit, else
			// Cyber (Technology), Plasma (Elements), Bio (Life), Necro (Supernatural) or Spacetime, in
			// that order of checks; with Knockback, the family's knockback death comes before it. Death
			// is the last; the client plays 0xF302994E when the noun has none of them.
			static std::vector<CharacterAnimationType> DeathAnimations(uint32_t hit);

			static constexpr uint32_t fallbackDeathState = 0xF302994E;
	};
}

#endif
