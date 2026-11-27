
#ifndef _GAME_NAVMESH_HEADER
#define _GAME_NAVMESH_HEADER

// Include
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Game
namespace Game {
	// The client walks every creature on its own BabelFlux NavPower navmesh, one .bfx per level with
	// seven layers (one per footprint radius). This is the server's copy: it reads the same file and
	// plans on it, so a path the server walks is the one the client's mover takes. scripts/navmesh_extract.py
	// writes the files to data/serverdata/navmesh/<name>.bfx, named as in the level's <navMesh> entry,
	// and the layer radii to data/serverdata/navmesh/navpowertuning.json (docs/sync.md, item 7).
	//
	// A port of the navigation package of darkspin (github.com/darkspinnet/darkspin, MIT license; see
	// LICENSES.md): the .bfx reader and its checks, planar projection, the island labelling, the
	// polygon search on the stored edge costs and the grid. The corner pass is a funnel instead of
	// darkspin's portal-midpoint straightening, from the prototype in research/sync/nav/bfx.py.
	//
	// Coordinates are the game's: z is up, and every distance between a point and the mesh is measured
	// sideways (x, y) only. Polygons are convex and wind counter-clockwise seen from +z, so the
	// inside of the edge from vertex i to vertex i+1 is on its left. All queries are const and need
	// no locking once the mesh is loaded.
	class NavMesh {
		public:
			static constexpr uint32_t noPolygon = 0xFFFFFFFFu;

			// How far a start or goal may be from the mesh before FindPath gives up on it.
			static constexpr float defaultProjectDistance = 10.f;

			// Polygons FindPath may take off the queue; the largest layer of the 60 levels has a few thousand.
			static constexpr size_t defaultMaxVisited = 8192;

			// The seven NavPowerTuning radii, for when navpowertuning.json is missing or does not parse.
			static constexpr float fallbackRadii[7] = { 0.71f, 1.06f, 1.24f, 1.41f, 1.59f, 1.77f, 3.89f };

			// The closest spot on a layer: on its polygon `polygon` (an index into the layer, not the
			// stored offset), in island `island` (1 and up), `distance` away sideways.
			struct Projection {
				glm::vec3 position {};
				uint32_t polygon = noPolygon;
				uint32_t island = 0;
				float distance = 0;
			};

			// Where WalkLine stopped. `reachedEnd` is false when the line left the mesh first; `point` is
			// then on the edge it left by.
			struct LineWalk {
				glm::vec3 point {};
				bool reachedEnd = false;
				uint32_t polygon = noPolygon;
			};

			// Reads and checks a .bfx. Any problem is logged and gives false and an empty mesh, however
			// short or damaged the data. The layer radii come from navpowertuning.json next to the file
			// if it is there, else they stay as they were (the fallback until set).
			bool Load(const std::filesystem::path& path);
			bool Parse(const uint8_t* data, size_t size);
			void Clear();

			// The radius of each layer, as the client's NavPowerTuning has them. LoadTuning reads the
			// "radius" of each layer in navpowertuning.json; they must be above 0 and rise.
			bool LoadTuning(const std::filesystem::path& path);
			bool ParseTuning(const std::string& text);
			void SetLayerRadii(std::vector<float> radii);

			bool IsLoaded() const;
			size_t GetLayerCount() const;
			size_t GetPolygonCount(uint32_t layer) const;
			uint32_t GetIslandCount(uint32_t layer) const;
			float GetLayerRadius(uint32_t layer) const;

			// The layer a creature of this footprint radius walks on: the smallest whose radius is at
			// least `radius` (0.1 at the least), the last one if none is, as the client's 0x9E6160 does.
			uint32_t LayerForRadius(float radius) const;

			// The layer GetClosestPositionFromPoint uses: the second to last.
			uint32_t LayerForPoint() const;

			// The closest spot on the layer within maxDistance sideways, or nothing. When two polygons are
			// equally close the one whose point is nearer the input's height wins, then the lower offset.
			// With `island` above 0 only that island is looked at.
			std::optional<Projection> Locate(const glm::vec3& point, uint32_t layer, float maxDistance,
				uint32_t island = 0) const;
			std::optional<glm::vec3> Project(const glm::vec3& point, uint32_t layer, float maxDistance) const;

			// The island (connected part of the layer) the closest spot within maxDistance is in, 0 if none.
			uint32_t GetIsland(const glm::vec3& point, uint32_t layer, float maxDistance) const;

			// The walk from `start` to `goal`: start and goal projected onto the mesh (the goal onto the
			// start's island), and the corners between them. Empty when either is further than
			// maxProject from the mesh, the goal is on another island or the search runs past maxVisited
			// polygons.
			std::vector<glm::vec3> FindPath(const glm::vec3& start, const glm::vec3& goal, uint32_t layer,
				float maxProject = defaultProjectDistance, size_t maxVisited = defaultMaxVisited) const;

			// Length of a path, 3D. The second form plans one first and is -1 when there is none.
			static float PathLength(const std::vector<glm::vec3>& path);
			float PathLength(const glm::vec3& start, const glm::vec3& goal, uint32_t layer) const;

			// Whether `to` is a spot a creature can walk to from `from`: `from` is on the mesh (within
			// defaultProjectDistance), `to` projects within `tolerance` of itself, on from's island.
			bool IsReachable(const glm::vec3& from, const glm::vec3& to, uint32_t layer, float tolerance = 0.01f) const;

			// Walks the straight line from `start` (put on the mesh first) toward `end`, sideways, across
			// polygon after polygon until the line ends or leaves the mesh.
			LineWalk WalkLine(const glm::vec3& start, const glm::vec3& end, uint32_t layer,
				float maxProject = defaultProjectDistance) const;

		private:
			struct Vertex {
				glm::vec3 position {};
				uint32_t neighbour = noPolygon; // index of the polygon through the edge from this vertex to the next
				uint32_t cost = 0;
			};

			struct Polygon {
				uint32_t offset = 0; // as stored: from the start of the layer
				uint32_t first = 0; // into Layer::vertices
				uint32_t count = 0;
				uint32_t island = 0;
				glm::vec3 boundsMin {};
				glm::vec3 boundsMax {};
				glm::vec3 normal {};
				float planeD = 0;
				float flatZ = 0;
			};

			struct Layer {
				float radius = 0;
				std::vector<Polygon> polygons;
				std::vector<Vertex> vertices;
				std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
				int32_t minCellX = 0;
				int32_t minCellY = 0;
				int32_t maxCellX = 0;
				int32_t maxCellY = 0;
				uint32_t islands = 0;
			};

			struct Portal {
				glm::vec3 left {};
				glm::vec3 right {};
			};

			static bool ParseLayer(const uint8_t* data, uint64_t size, uint64_t offset, uint32_t index, Layer& layer,
				uint64_t& next, std::string& error);
			static bool LinkPolygons(Layer& layer, std::string& error);
			static bool BuildGrid(Layer& layer, std::string& error);

			void Candidates(const Layer& layer, const glm::vec3& point, float maxDistance, std::vector<uint32_t>& out) const;
			float HeightAt(const Polygon& polygon, float x, float y) const;
			bool Corridor(const Layer& layer, uint32_t from, uint32_t to, size_t maxVisited,
				std::vector<uint32_t>& polygons, std::vector<uint32_t>& edges) const;
			std::vector<glm::vec3> Funnel(const Layer& layer, const glm::vec3& start, const glm::vec3& goal,
				const std::vector<uint32_t>& polygons, const std::vector<uint32_t>& edges) const;

			std::vector<Layer> mLayers;
			std::vector<float> mRadii { std::begin(fallbackRadii), std::end(fallbackRadii) };
	};
}

#endif
