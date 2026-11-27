
#ifndef _GAME_BEHAVIORRUNNER_HEADER
#define _GAME_BEHAVIORRUNNER_HEADER

// Include
#include <cstdint>
#include <string>

// Game
namespace Game {
	class Object;

	// BehaviorRunner
	// Runs one AI behavior for an agent as the client does. A behavior is a global table
	// (nBehavior_FollowOwner): [1] runs once when it starts (0xa68f10), [2] runs as a coroutine
	// that the server resumes every update for as long as it waits (0xa68eb0), and [3] runs when it
	// is left (0xa68e10), where every pet behavior stops its agent. Each step runs on its own
	// thread, which nBehaviorTree.GetMyObjectID reads its agent from.
	// The three steps share one data table, as they share one context in the client: 0xa68f10
	// clears the context's slots and points the agent's behavior thread at it (+0xe0) before [1]
	// runs and [2] starts, and 0xa68e10 (0xa68e60 is its twin) runs [3] with the same context, read
	// from the agent's behavior thread (+0x2a4) like the other two steps. nThreadData reads it there
	// (dataOwner in scriptapi.cpp), so nBehavior_Invisible's [3] removes the Intangible its [1]
	// added, and nBehavior_Wander's [2] walks to the point its [1] picked.
	class BehaviorRunner {
		public:
			// The thread value GetMyObjectID reads.
			static constexpr const char* agentKey = "behaviorAgent";
			// The thread value nThreadData reads the running behavior's shared table from. The table is
			// kept in the agent's private table under the same name (Lua::CreatePrivateTable), so it
			// lives no longer than the agent and the Lua state.
			static constexpr const char* dataKey = "behaviorData";

			explicit BehaviorRunner(Object& agent);

			BehaviorRunner(const BehaviorRunner&) = delete;
			BehaviorRunner& operator=(const BehaviorRunner&) = delete;

			// Starts the named behavior unless it runs already; one that ended starts again. A name
			// no script defines leaves the current behavior and runs nothing.
			void Run(const std::string& name);

			// Leaves the running behavior, if any: its coroutine stops and its exit step runs.
			// Call it before the agent goes (ObjectManager), while its scripts can still reach it.
			void Exit();

		private:
			bool Call(const std::string& name, int step, bool keep);

			// A started behavior gets a new, empty table; it goes once its exit step has run.
			void NewData();
			void DropData();

		private:
			Object& mAgent;
			std::string mName;
			uint64_t mTicket = 0;
			// No script defines mName, so it is not started again until the name changes.
			bool mMissing = false;
	};
}

#endif
