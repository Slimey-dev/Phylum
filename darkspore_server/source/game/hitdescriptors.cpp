
// Include
#include "hitdescriptors.h"
#include "noun.h"

#include "utils/functions.h"

// Game
namespace Game {
	uint32_t HitDescriptors::Of(bool critical, DamageSource source, DamageType type, Descriptors descriptors, bool knockback) {
		const auto flags = utils::enum_wrapper { descriptors };

		uint32_t hit = critical ? Critical : 0;
		hit |= source == DamageSource::Physical ? SourcePhysical : SourceEnergy;
		if (const auto index = static_cast<uint32_t>(type); index <= static_cast<uint32_t>(DamageType::Supernatural)) {
			hit |= TypeTechnology << index;
		}
		if (flags.test(Descriptors::IsAoE)) {
			hit |= AoE;
		}
		if (flags.test(Descriptors::IsDoT)) {
			hit |= DoT;
		}
		if (knockback) {
			hit |= Knockback;
		}
		return hit;
	}

	std::vector<CharacterAnimationType> HitDescriptors::DeathAnimations(uint32_t hit) {
		if (!(hit & Critical)) {
			return { CharacterAnimationType::MeleeDeath, CharacterAnimationType::Death };
		}

		// Each family's critical death is followed by its knockback one in CharacterAnimationType.
		CharacterAnimationType family = CharacterAnimationType::SpacetimeCriticalDeath;
		if (hit & SourcePhysical) {
			family = CharacterAnimationType::MeleeCriticalDeath;
		} else if (hit & TypeTechnology) {
			family = CharacterAnimationType::CyberCriticalDeath;
		} else if (hit & TypeElements) {
			family = CharacterAnimationType::PlasmaCriticalDeath;
		} else if (hit & TypeLife) {
			family = CharacterAnimationType::BioCriticalDeath;
		} else if (hit & TypeSupernatural) {
			family = CharacterAnimationType::NecroCriticalDeath;
		}

		std::vector<CharacterAnimationType> deaths;
		if (hit & Knockback) {
			deaths.push_back(utils::enum_helper<CharacterAnimationType>::next(family, 1));
		}
		deaths.push_back(family);
		deaths.push_back(CharacterAnimationType::Death);
		return deaths;
	}
}
