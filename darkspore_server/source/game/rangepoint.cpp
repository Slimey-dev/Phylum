
// Include
#include "rangepoint.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// Game
namespace Game {
	glm::vec3 RangePoint::Pick(const glm::vec3& center, const glm::quat& orientation, float range, bool radial, float u, float v) {
		range = std::max(range, 0.f);
		u = std::clamp(u, 0.f, 1.f);
		v = std::clamp(v, 0.f, 1.f);

		if (radial) {
			const float angle = 2.f * std::numbers::pi_v<float> * u;
			const float distance = range * v;
			return center + glm::vec3(std::cos(angle), std::sin(angle), 0.f) * distance;
		}

		// The square is the centre's own, so it turns with it; height stays the centre's.
		glm::vec3 offset = orientation * glm::vec3((2.f * u - 1.f) * range, (2.f * v - 1.f) * range, 0.f);
		offset.z = 0.f;
		return center + offset;
	}
}
