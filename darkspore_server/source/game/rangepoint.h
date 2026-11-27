
#ifndef _GAME_RANGEPOINT_HEADER
#define _GAME_RANGEPOINT_HEADER

// Include
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Game
namespace Game {
	// RangePoint
	// A random point within range of an object, as nGameObject.GetPositionWithinRangeOfObject picks it
	// (Darkspore.exe 0xa0dbe0, its four arguments: agent, centre object, range, flag). Every game script
	// passes the flag off (nBehavior_Wander, Stroll and Follow): the point is then anywhere in a square of
	// half-side range around the centre, turned with the centre's orientation (0xa0c970 draws each side
	// between -range and range). With the flag on it lies in a random direction at a random distance up
	// to range (0xa0ca20, 0xae52a0). A negative range is 0.
	// Approximate: the client lifts the point by half the agent's size and keeps it only when its
	// physics finds ground there, trying up to 20 points. The server has no ground, so the first point
	// counts and stays at the centre's height.
	class RangePoint {
		public:
			// u and v are two random draws in [0, 1).
			static glm::vec3 Pick(const glm::vec3& center, const glm::quat& orientation, float range, bool radial, float u, float v);
	};
}

#endif
