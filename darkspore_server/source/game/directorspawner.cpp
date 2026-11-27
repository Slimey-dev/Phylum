
// Include
#include "directorspawner.h"

#include <algorithm>
#include <array>
#include <utility>

// Game
namespace Game {
	namespace {
		bool AnyHeroWithin(const glm::vec3& position, const std::vector<glm::vec3>& heroes, float radius) {
			const float radiusSquared = radius * radius;
			return std::any_of(heroes.begin(), heroes.end(), [&](const glm::vec3& hero) {
				const glm::vec3 offset = hero - position;
				return glm::dot(offset, offset) <= radiusSquared;
			});
		}

		uint32_t ValidSection(uint32_t section) {
			return section < directorSectionCount ? section : directorLevelSection;
		}

		glm::vec3 Scatter(const glm::vec3& position, DirectorRandom& random) {
			const float x = (random.Unit() * 2.f - 1.f) * directorSpawnScatter;
			const float y = (random.Unit() * 2.f - 1.f) * directorSpawnScatter;
			return position + glm::vec3(x, y, 0.f);
		}
	}

	// DirectorSpawner
	void DirectorSpawner::AddWanderPoint(const glm::vec3& position, uint32_t section) {
		WanderPoint point;
		point.position = position;
		point.section = ValidSection(section);
		mWanderPoints.push_back(point);
	}

	void DirectorSpawner::AddSpikeSet(std::string name, std::vector<SpikePoint> points, uint32_t section) {
		if (points.empty()) {
			return;
		}

		SpikeSet set;
		set.name = std::move(name);
		set.points = std::move(points);
		set.section = ValidSection(section);
		mSpikeSets.push_back(std::move(set));
	}

	std::vector<DirectorSpawnRequest> DirectorSpawner::Update(const std::vector<glm::vec3>& heroes, bool doingWell,
		const DirectorPool& pool, uint32_t difficulty, DirectorRandom& random
	) {
		std::vector<DirectorSpawnRequest> spawns;
		for (size_t i = 0; i < mWanderPoints.size(); ++i) {
			UpdateWanderPoint(mWanderPoints[i], i, heroes, pool, random, spawns);
		}
		for (auto& set : mSpikeSets) {
			UpdateSpikeSet(set, heroes, doingWell, pool, difficulty, random, spawns);
		}
		return spawns;
	}

	void DirectorSpawner::UpdateWanderPoint(WanderPoint& point, size_t index, const std::vector<glm::vec3>& heroes,
		const DirectorPool& pool, DirectorRandom& random, std::vector<DirectorSpawnRequest>& spawns
	) {
		// RollWanderHit gives nothing after the fifth hit, so such a point is done too.
		constexpr uint32_t lastHit = 5;
		if (point.spawned || point.hits >= lastHit) {
			return;
		}

		const bool near = AnyHeroWithin(point.position, heroes, directorWanderHitRadius);
		const bool arrived = point.seen && near && !point.heroNear;
		point.seen = true;
		point.heroNear = near;
		if (!arrived) {
			return;
		}

		const uint32_t count = RollWanderHit(point.hits, random);
		++point.hits;
		if (count == 0) {
			return;
		}

		point.spawned = true;
		for (uint32_t i = 0; i < count; ++i) {
			const uint32_t noun = PickWanderer(pool, point.section, random);
			if (noun == 0) {
				continue;
			}

			DirectorSpawnRequest request;
			request.noun = noun;
			request.position = Scatter(point.position, random);
			request.source = "wander";
			request.point = std::to_string(index);
			spawns.push_back(std::move(request));
		}
	}

	void DirectorSpawner::UpdateSpikeSet(SpikeSet& set, const std::vector<glm::vec3>& heroes, bool doingWell,
		const DirectorPool& pool, uint32_t difficulty, DirectorRandom& random, std::vector<DirectorSpawnRequest>& spawns
	) {
		if (set.fired) {
			return;
		}

		const bool near = std::any_of(set.points.begin(), set.points.end(), [&](const SpikePoint& point) {
			return AnyHeroWithin(point.position, heroes, directorSpikeTriggerRadius);
		});
		const bool arrived = set.seen && near && !set.heroNear;
		set.seen = true;
		set.heroNear = near;
		if (!arrived) {
			return;
		}

		// A skipped spike is spent too: the talk counts it in the last two.
		set.fired = true;
		const SpikeIntensity intensity = mSpikeHistory.Advance(doingWell);
		const int32_t budget = SpikeBudget(set.section, intensity, difficulty);
		if (budget <= 0) {
			return;
		}

		const auto nouns = BuildMixedGroup(pool, set.section, budget, difficulty, random);
		for (size_t i = 0; i < nouns.size(); ++i) {
			// One creature per point, then round again, so a large group spreads over the set.
			const auto& point = set.points[i % set.points.size()];

			DirectorSpawnRequest request;
			request.noun = nouns[i];
			request.position = Scatter(point.position, random);
			request.orientation = point.orientation;
			request.source = "spike";
			request.point = set.name;
			spawns.push_back(std::move(request));
		}
	}

	size_t DirectorSpawner::GetWanderPointCount() const {
		return mWanderPoints.size();
	}

	size_t DirectorSpawner::GetSpikeSetCount() const {
		return mSpikeSets.size();
	}

	const SpikeHistory& DirectorSpawner::GetSpikeHistory() const {
		return mSpikeHistory;
	}

	// MostCommonSection
	uint32_t MostCommonSection(const std::vector<uint32_t>& sections) {
		std::array<uint32_t, directorSectionCount> counts {};
		for (const uint32_t section : sections) {
			++counts[ValidSection(section)];
		}

		uint32_t best = directorLevelSection;
		uint32_t bestCount = 0;
		for (uint32_t section = 0; section < directorLevelSection; ++section) {
			if (counts[section] > bestCount) {
				best = section;
				bestCount = counts[section];
			}
		}
		return best;
	}
}
