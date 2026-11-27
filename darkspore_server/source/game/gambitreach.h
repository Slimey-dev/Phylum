
#ifndef _GAME_GAMBITREACH_HEADER
#define _GAME_GAMBITREACH_HEADER

// Game
namespace Game {
	// GambitReach
	// Whether an AI gambit goes when its target may be out of its ability's range. The client's
	// ability admission (Darkspore.exe 0x9e0660) answers 1 for a cast in range, 3 ("pursue") for one
	// out of range whose ability has shouldPursue (+0x151), and refuses any other out-of-range cast
	// (-0x270c). An agent that cannot walk (a turret) pursues nothing.
	class GambitReach {
		public:
			enum class Answer {
				Cast,
				Pursue,
				// Out of range, and the agent may not or cannot walk up for it.
				TooFar
			};

			static Answer Decide(bool inReach, bool shouldPursue, bool canWalk);
	};
}

#endif
