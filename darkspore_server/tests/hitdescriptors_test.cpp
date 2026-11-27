#include <doctest/doctest.h>

#include "game/hitdescriptors.h"
#include "game/noun.h"

using Game::CharacterAnimationType;
using Game::DamageSource;
using Game::DamageType;
using Game::Descriptors;
using Game::HitDescriptors;

namespace {
	Descriptors Flags(std::initializer_list<Descriptors> list) {
		uint32_t value = 0;
		for (const auto flag : list) {
			value |= static_cast<uint32_t>(flag);
		}
		return static_cast<Descriptors>(value);
	}
}

TEST_CASE("a hit's bits: critical, its source and its element, as nHitDescriptors names them") {
	// A Zelem shooter's energy shot (Spacetime) that was not critical.
	CHECK(HitDescriptors::Of(false, DamageSource::Energy, DamageType::Spacetime, Flags({ Descriptors::IsProjectile }), false) == (4u | 16u));
	// A critical Technology swing.
	CHECK(HitDescriptors::Of(true, DamageSource::Physical, DamageType::Technology, Flags({ Descriptors::IsMelee, Descriptors::IsBasic }), false) == (1u | 2u | 8u));
	CHECK(HitDescriptors::Of(false, DamageSource::Physical, DamageType::Supernatural, Flags({}), false) == (2u | 128u));
}

TEST_CASE("a generic hit has no element, and area, damage over time and knockback add their bits") {
	CHECK(HitDescriptors::Of(false, DamageSource::Energy, DamageType::Generic, Flags({}), false) == HitDescriptors::SourceEnergy);
	const auto hit = HitDescriptors::Of(true, DamageSource::Energy, DamageType::Life, Flags({ Descriptors::IsAoE, Descriptors::IsDoT }), true);
	CHECK(hit == (1u | 4u | 32u | 256u | 512u | 1024u));
}

TEST_CASE("only the Critical bit is what ShouldRepair checks for") {
	CHECK((HitDescriptors::Of(true, DamageSource::Energy, DamageType::Elements, Flags({}), false) & HitDescriptors::Critical) != 0);
	CHECK((HitDescriptors::Of(false, DamageSource::Energy, DamageType::Elements, Flags({}), false) & HitDescriptors::Critical) == 0);
}

TEST_CASE("a hit that was not critical kills with the melee death, else the plain one") {
	const auto deaths = HitDescriptors::DeathAnimations(HitDescriptors::SourceEnergy | HitDescriptors::TypeTechnology);
	REQUIRE(deaths.size() == 2);
	CHECK(deaths[0] == CharacterAnimationType::MeleeDeath);
	CHECK(deaths[1] == CharacterAnimationType::Death);
	// Knockback does not matter without a critical hit.
	CHECK(HitDescriptors::DeathAnimations(HitDescriptors::Knockback).size() == 2);
}

TEST_CASE("a critical hit picks its family: physical first, then the element") {
	using H = HitDescriptors;
	CHECK(H::DeathAnimations(H::Critical | H::SourcePhysical | H::TypeTechnology)[0] == CharacterAnimationType::MeleeCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeTechnology)[0] == CharacterAnimationType::CyberCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeElements)[0] == CharacterAnimationType::PlasmaCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeLife)[0] == CharacterAnimationType::BioCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeSupernatural)[0] == CharacterAnimationType::NecroCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeSpacetime)[0] == CharacterAnimationType::SpacetimeCriticalDeath);
	CHECK(H::DeathAnimations(H::Critical | H::SourceEnergy)[0] == CharacterAnimationType::SpacetimeCriticalDeath);
}

TEST_CASE("a critical knockback tries the family's knockback death first, and Death last") {
	using H = HitDescriptors;
	const auto deaths = H::DeathAnimations(H::Critical | H::SourceEnergy | H::TypeLife | H::Knockback);
	REQUIRE(deaths.size() == 3);
	CHECK(deaths[0] == CharacterAnimationType::BioCriticalKnockbackDeath);
	CHECK(deaths[1] == CharacterAnimationType::BioCriticalDeath);
	CHECK(deaths[2] == CharacterAnimationType::Death);
}
