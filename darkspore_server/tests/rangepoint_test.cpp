#include <doctest/doctest.h>

#include "game/rangepoint.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <initializer_list>

using Game::RangePoint;

namespace {
	const glm::quat facingY = glm::quat(1.f, 0.f, 0.f, 0.f);
	// A quarter turn about +Z.
	const glm::quat quarterTurn = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
}

TEST_CASE("the square's corners and centre come from the draws, around the centre object") {
	const glm::vec3 center(-140.f, -80.f, 12.f);
	const auto low = RangePoint::Pick(center, facingY, 5.f, false, 0.f, 0.f);
	CHECK(low.x == doctest::Approx(-145.f));
	CHECK(low.y == doctest::Approx(-85.f));
	CHECK(low.z == doctest::Approx(12.f));

	const auto middle = RangePoint::Pick(center, facingY, 5.f, false, 0.5f, 0.5f);
	CHECK(middle.x == doctest::Approx(-140.f));
	CHECK(middle.y == doctest::Approx(-80.f));

	const auto high = RangePoint::Pick(center, facingY, 5.f, false, 1.f, 1.f);
	CHECK(high.x == doctest::Approx(-135.f));
	CHECK(high.y == doctest::Approx(-75.f));
}

TEST_CASE("the square turns with the centre and stays at its height") {
	// +X in the centre's frame is +Y once it has turned a quarter.
	const auto point = RangePoint::Pick(glm::vec3(0.f, 0.f, 3.f), quarterTurn, 4.f, false, 1.f, 0.5f);
	CHECK(point.x == doctest::Approx(0.f).epsilon(0.0001));
	CHECK(point.y == doctest::Approx(4.f));
	CHECK(point.z == doctest::Approx(3.f));
}

TEST_CASE("with the flag, the point is at a random distance up to range in a random direction") {
	const glm::vec3 center(10.f, 20.f, 0.f);
	for (float u : { 0.f, 0.3f, 0.75f }) {
		for (float v : { 0.f, 0.5f, 1.f }) {
			const auto point = RangePoint::Pick(center, quarterTurn, 6.f, true, u, v);
			CHECK(glm::distance(point, center) == doctest::Approx(6.f * v));
			CHECK(point.z == doctest::Approx(0.f));
		}
	}
}

TEST_CASE("a negative range keeps the point on the centre, and every point is within range") {
	const glm::vec3 center(1.f, 2.f, 3.f);
	const auto point = RangePoint::Pick(center, facingY, -2.f, false, 0.f, 1.f);
	CHECK(point.x == doctest::Approx(1.f));
	CHECK(point.y == doctest::Approx(2.f));

	// Wander's radius is 5: no point lies farther along either side of the square.
	for (float u = 0.f; u <= 1.f; u += 0.125f) {
		const auto corner = RangePoint::Pick(center, quarterTurn, 5.f, false, u, 1.f - u);
		CHECK(std::abs(corner.x - center.x) <= 5.0001f);
		CHECK(std::abs(corner.y - center.y) <= 5.0001f);
	}
}
