
#include <doctest/doctest.h>

#include "game/navmesh.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

using Game::NavMesh;

namespace {
	// Builds .bfx bytes for the tests, in the layout NavMesh reads (notes/client/navmesh.md of darkspin).
	struct TestPolygon {
		std::vector<glm::vec3> corners; // counter-clockwise from +z
		std::vector<int> neighbours; // per edge: the polygon over it, or -1
	};

	struct TestLayer {
		std::vector<TestPolygon> polygons;
		float radius = 1.f;
	};

	constexpr uint32_t headerSize = 0x13C;
	constexpr uint32_t prefixSize = 0x34;
	constexpr uint32_t edgeSize = 0x18;
	constexpr uint32_t treeSize = 0x40;

	void Put32(std::vector<uint8_t>& bytes, uint32_t value) {
		for (int shift = 0; shift < 32; shift += 8) {
			bytes.push_back(static_cast<uint8_t>(value >> shift));
		}
	}

	void PutFloat(std::vector<uint8_t>& bytes, float value) {
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		Put32(bytes, bits);
	}

	void PutVec3(std::vector<uint8_t>& bytes, const glm::vec3& value) {
		PutFloat(bytes, value.x);
		PutFloat(bytes, value.y);
		PutFloat(bytes, value.z);
	}

	void Set32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
		for (int index = 0; index < 4; ++index) {
			bytes[offset + index] = static_cast<uint8_t>(value >> (8 * index));
		}
	}

	uint32_t Get32(const std::vector<uint8_t>& bytes, size_t offset) {
		uint32_t value = 0;
		for (int index = 0; index < 4; ++index) {
			value |= static_cast<uint32_t>(bytes[offset + index]) << (8 * index);
		}
		return value;
	}

	std::vector<uint8_t> BuildLayer(const TestLayer& layer, uint32_t index) {
		std::vector<uint32_t> offsets;
		uint32_t arena = 0;
		glm::vec3 low(1e30f), high(-1e30f);
		for (const TestPolygon& polygon : layer.polygons) {
			offsets.push_back(headerSize + arena);
			arena += prefixSize + edgeSize * static_cast<uint32_t>(polygon.corners.size());
			for (const glm::vec3& corner : polygon.corners) {
				low = glm::min(low, corner);
				high = glm::max(high, corner);
			}
		}

		std::vector<uint8_t> bytes;
		Put32(bytes, 0x1C);
		Put32(bytes, index);
		Put32(bytes, arena);
		Put32(bytes, headerSize + arena + 0x1C + treeSize);
		PutFloat(bytes, 1.f); // build scale
		PutFloat(bytes, 0.15f); // quantum
		PutFloat(bytes, layer.radius);
		PutFloat(bytes, 0.5f); // step
		PutFloat(bytes, 2.f); // height
		PutVec3(bytes, low);
		PutVec3(bytes, high);
		bytes.resize(headerSize, 0);

		for (const TestPolygon& polygon : layer.polygons) {
			glm::vec3 center(0);
			for (const glm::vec3& corner : polygon.corners) {
				center += corner;
			}
			center /= static_cast<float>(polygon.corners.size());
			float radius = 0;
			for (const glm::vec3& corner : polygon.corners) {
				radius = std::max(radius, glm::distance(corner, center));
			}

			bytes.resize(bytes.size() + 0x10, 0);
			PutVec3(bytes, center);
			PutFloat(bytes, radius);
			Put32(bytes, 0); // user data
			Put32(bytes, 0);
			Put32(bytes, static_cast<uint32_t>(polygon.corners.size()) | 0xFFFFu << 7 | index << 23);
			Put32(bytes, 0x2110000);
			Put32(bytes, 0);
			for (size_t edge = 0; edge < polygon.corners.size(); ++edge) {
				const int neighbour = polygon.neighbours[edge];
				Put32(bytes, neighbour < 0 ? 0 : offsets[neighbour]);
				PutVec3(bytes, polygon.corners[edge]);
				Put32(bytes, 0xFFFF0000u);
				Put32(bytes, neighbour < 0 ? 0 : 1000);
			}
		}

		PutVec3(bytes, low);
		PutVec3(bytes, high);
		Put32(bytes, treeSize);
		bytes.resize(bytes.size() + treeSize, 0);
		return bytes;
	}

	std::vector<uint8_t> BuildMesh(const std::vector<TestLayer>& layers) {
		std::vector<uint8_t> body;
		for (uint32_t index = 0; index < layers.size(); ++index) {
			const auto layer = BuildLayer(layers[index], index);
			body.insert(body.end(), layer.begin(), layer.end());
		}

		std::vector<uint8_t> bytes;
		Put32(bytes, 0);
		Put32(bytes, 2);
		Put32(bytes, static_cast<uint32_t>(body.size() + 0x18));
		Put32(bytes, 0);
		Put32(bytes, 0);
		Put32(bytes, 0);
		Put32(bytes, 0x10000);
		Put32(bytes, static_cast<uint32_t>(body.size() + 0xC));
		Put32(bytes, 0);
		Put32(bytes, 0);
		Put32(bytes, 0x1C);
		Put32(bytes, static_cast<uint32_t>(layers.size()));
		bytes.insert(bytes.end(), body.begin(), body.end());
		return bytes;
	}

	// A field of rectangular cells between the lines xs and ys; cell (x, y) is polygon number
	// y * (xs.size() - 1) + x counted without the holes. The height of the ground is slope * x.
	TestLayer Field(const std::vector<float>& xs, const std::vector<float>& ys, const std::set<std::pair<int, int>>& holes = {},
		float slope = 0, float radius = 1.f) {
		const int width = static_cast<int>(xs.size()) - 1;
		const int height = static_cast<int>(ys.size()) - 1;
		std::map<std::pair<int, int>, int> index;
		TestLayer layer;
		layer.radius = radius;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (!holes.count({ x, y })) {
					index[{ x, y }] = static_cast<int>(layer.polygons.size());
					layer.polygons.emplace_back();
				}
			}
		}
		for (const auto& [at, polygon] : index) {
			const auto [x, y] = at;
			const float x0 = xs[x], x1 = xs[x + 1], y0 = ys[y], y1 = ys[y + 1];
			TestPolygon& made = layer.polygons[polygon];
			made.corners = { { x0, y0, slope * x0 }, { x1, y0, slope * x1 }, { x1, y1, slope * x1 }, { x0, y1, slope * x0 } };
			const std::pair<int, int> around[4] = { { x, y - 1 }, { x + 1, y }, { x, y + 1 }, { x - 1, y } };
			for (const auto& next : around) {
				const auto found = index.find(next);
				made.neighbours.push_back(found == index.end() ? -1 : found->second);
			}
		}
		return layer;
	}

	// A width x height field of square cells.
	TestLayer Grid(int width, int height, const std::set<std::pair<int, int>>& holes = {}, float cell = 2.f,
		glm::vec2 origin = { 0, 0 }, float slope = 0, float radius = 1.f) {
		std::vector<float> xs, ys;
		for (int x = 0; x <= width; ++x) {
			xs.push_back(origin.x + x * cell);
		}
		for (int y = 0; y <= height; ++y) {
			ys.push_back(origin.y + y * cell);
		}
		return Field(xs, ys, holes, slope, radius);
	}

	// 7 x 7 cells of 2 m with a 3 x 3 block missing from the middle: x and y from 4 to 10 are empty.
	const std::set<std::pair<int, int>> blockHoles = {
		{ 2, 2 }, { 3, 2 }, { 4, 2 }, { 2, 3 }, { 3, 3 }, { 4, 3 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };

	// A square of 14 m with a block of 6 m missing from the middle, as a frame of eight polygons with
	// the bottom middle one gone too: the only way from the left side to the right is over the top.
	TestLayer Frame(float slope = 0) {
		return Field({ 0, 4, 10, 14 }, { 0, 4, 10, 14 }, { { 1, 1 }, { 1, 0 } }, slope);
	}

	std::vector<uint8_t> BlockMesh() {
		// Layer 0: the block field. Layer 1: the same, on a slope. Layer 2: two islands 100 m apart.
		// Layer 3: the frame. Layer 4: the frame, on a slope.
		TestLayer islands = Grid(2, 2);
		const TestLayer far = Grid(2, 2, {}, 2.f, { 100, 0 });
		const int shift = static_cast<int>(islands.polygons.size());
		for (TestPolygon polygon : far.polygons) {
			for (int& neighbour : polygon.neighbours) {
				neighbour = neighbour < 0 ? -1 : neighbour + shift;
			}
			islands.polygons.push_back(polygon);
		}
		return BuildMesh({ Grid(7, 7, blockHoles), Grid(7, 7, blockHoles, 2.f, { 0, 0 }, 0.5f), islands, Frame(), Frame(0.5f) });
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f) {
		return glm::distance(a, b) <= tolerance;
	}

	// Every point along every segment of a path must be on the mesh.
	bool StaysOnMesh(const NavMesh& mesh, const std::vector<glm::vec3>& path, uint32_t layer) {
		for (size_t index = 1; index < path.size(); ++index) {
			const int steps = std::max(1, static_cast<int>(glm::distance(path[index - 1], path[index]) / 0.05f));
			for (int step = 0; step <= steps; ++step) {
				const glm::vec3 point = glm::mix(path[index - 1], path[index], static_cast<float>(step) / steps);
				const auto found = mesh.Locate(point, layer, 0.01f);
				if (!found) {
					std::cout << "off the mesh at " << point.x << ", " << point.y << ", " << point.z << std::endl;
					return false;
				}
			}
		}
		return true;
	}

	// NavMesh says why it refuses a file; the tests that feed it damaged ones would drown in it.
	class QuietLog : private std::streambuf {
		public:
			QuietLog() : mOld(std::cout.rdbuf(this)) {}
			~QuietLog() override { std::cout.rdbuf(mOld); }

		private:
			int overflow(int c) override { return c; }

			std::streambuf* mOld;
	};

	// Game files stay out of the repo: the fixtures are read where they are.
	std::filesystem::path FindFixture(const std::vector<std::string>& names) {
		std::vector<std::filesystem::path> roots;
		if (const char* given = std::getenv("NAVMESH_FIXTURE_DIR")) {
			roots.emplace_back(given);
		}
		// <repo>/darkspore_server/tests/navmesh_test.cpp, the research notes next to the checkout.
		const std::filesystem::path source = std::filesystem::absolute(__FILE__);
		const std::filesystem::path repo = source.parent_path().parent_path().parent_path();
		roots.push_back(repo.parent_path() / "research" / "sync" / "nav" / "assets");
		roots.push_back("data/serverdata/navmesh");
		roots.push_back("data/navmesh");
		if (const char* home = std::getenv("HOME")) {
			roots.push_back(std::filesystem::path(home) / ".local/share/Steam/steamapps/common/Darkspore/DarksporeBin/Server/data/serverdata/navmesh");
		}
		for (const auto& root : roots) {
			for (const auto& name : names) {
				std::error_code error;
				if (std::filesystem::is_regular_file(root / name, error)) {
					return root / name;
				}
			}
		}
		return {};
	}
}

TEST_CASE("a mesh built in memory loads, with its layers, polygons and islands") {
	const auto bytes = BlockMesh();
	NavMesh mesh;

	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));
	CHECK(mesh.IsLoaded());
	CHECK(mesh.GetLayerCount() == 5);
	CHECK(mesh.GetPolygonCount(0) == 40);
	CHECK(mesh.GetPolygonCount(1) == 40);
	CHECK(mesh.GetPolygonCount(2) == 8);
	CHECK(mesh.GetPolygonCount(3) == 7);
	CHECK(mesh.GetPolygonCount(5) == 0);
	CHECK(mesh.GetIslandCount(0) == 1);
	CHECK(mesh.GetIslandCount(2) == 2);
	CHECK(mesh.LayerForPoint() == 3);
}

TEST_CASE("a mesh holding nothing, or not a mesh, is refused") {
	const QuietLog quiet;
	NavMesh mesh;
	const std::vector<uint8_t> nothing;
	const std::vector<uint8_t> junk(500, 0x5A);

	CHECK_FALSE(mesh.Parse(nullptr, 0));
	CHECK_FALSE(mesh.Parse(nothing.data(), nothing.size()));
	CHECK_FALSE(mesh.Parse(junk.data(), junk.size()));
	CHECK_FALSE(mesh.IsLoaded());
	CHECK_FALSE(mesh.Load("this/file/is/not/there.bfx"));
	CHECK(mesh.GetLayerCount() == 0);
	CHECK_FALSE(mesh.Project({ 0, 0, 0 }, 0, 10).has_value());
	CHECK(mesh.FindPath({ 0, 0, 0 }, { 1, 1, 0 }, 0).empty());
	CHECK_FALSE(mesh.IsReachable({ 0, 0, 0 }, { 1, 1, 0 }, 0));
}

TEST_CASE("a failed load leaves no mesh behind") {
	const QuietLog quiet;
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	auto broken = bytes;
	broken.pop_back();
	CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	CHECK_FALSE(mesh.IsLoaded());
}

TEST_CASE("a mesh cut off anywhere is refused, never read past its end") {
	const QuietLog quiet;
	const auto bytes = BlockMesh();
	NavMesh mesh;

	for (size_t length = 0; length < bytes.size(); ++length) {
		const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + length);
		REQUIRE_FALSE(mesh.Parse(cut.data(), cut.size()));
	}
	CHECK(mesh.Parse(bytes.data(), bytes.size()));
}

TEST_CASE("a mesh with a damaged field is refused") {
	const QuietLog quiet;
	const auto bytes = BlockMesh();
	NavMesh mesh;
	const size_t layer0 = 0x30;
	const size_t polygon0 = layer0 + headerSize;
	const size_t firstEdge = polygon0 + prefixSize;

	SUBCASE("the envelope size") {
		auto broken = bytes;
		Set32(broken, 8, Get32(broken, 8) + 4);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("the image size") {
		auto broken = bytes;
		Set32(broken, 0x1C, Get32(broken, 0x1C) - 4);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("the layer count") {
		auto broken = bytes;
		Set32(broken, 0x2C, 0);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
		Set32(broken, 0x2C, 4);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
		Set32(broken, 0x2C, 1000);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a layer's own index") {
		auto broken = bytes;
		Set32(broken, layer0 + 4, 5);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a layer's size") {
		auto broken = bytes;
		Set32(broken, layer0 + 0xC, 0xFFFFFFF0u);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
		broken = bytes;
		Set32(broken, layer0 + 8, Get32(broken, layer0 + 8) + 1);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a polygon's edge count") {
		auto broken = bytes;
		Set32(broken, polygon0 + 0x28, 2 | 0xFFFFu << 7);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
		Set32(broken, polygon0 + 0x28, 127 | 0xFFFFu << 7);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a polygon's layer") {
		auto broken = bytes;
		Set32(broken, polygon0 + 0x28, 4 | 0xFFFFu << 7 | 3u << 23);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a neighbour that is not a polygon") {
		auto broken = bytes;
		Set32(broken, firstEdge, 0x123456);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a polygon its own neighbour") {
		auto broken = bytes;
		Set32(broken, firstEdge + edgeSize, headerSize);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a link with no reverse") {
		// Polygon 0 loses its link to the polygon at its right, which still links back.
		auto broken = bytes;
		Set32(broken, firstEdge + edgeSize, 0);
		Set32(broken, firstEdge + edgeSize + 0x14, 0);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a link with a cost and no neighbour") {
		auto broken = bytes;
		Set32(broken, firstEdge + edgeSize, 0);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a corner that is not a number") {
		auto broken = bytes;
		Set32(broken, firstEdge + 4, 0x7FC00000);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a polygon wound clockwise") {
		auto broken = bytes;
		for (size_t axis = 0; axis < 3; ++axis) {
			const uint32_t a = Get32(broken, firstEdge + 4 + 4 * axis);
			Set32(broken, firstEdge + 4 + 4 * axis, Get32(broken, firstEdge + edgeSize * 2 + 4 + 4 * axis));
			Set32(broken, firstEdge + edgeSize * 2 + 4 + 4 * axis, a);
		}
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("a tree size that does not add up") {
		auto broken = bytes;
		const size_t layerSize = Get32(bytes, layer0 + 0xC);
		Set32(broken, layer0 + layerSize - treeSize - 4, treeSize + 1);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	SUBCASE("data after the last layer") {
		auto broken = bytes;
		broken.insert(broken.end(), 8, 0);
		Set32(broken, 8, Get32(broken, 8) + 8);
		Set32(broken, 0x1C, Get32(broken, 0x1C) + 8);
		CHECK_FALSE(mesh.Parse(broken.data(), broken.size()));
	}
	CHECK_FALSE(mesh.IsLoaded());
}

TEST_CASE("a mesh with any byte changed is read or refused, without a crash") {
	const QuietLog quiet;
	const auto bytes = BlockMesh();
	NavMesh mesh;
	std::mt19937 random(7);

	for (int round = 0; round < 3000; ++round) {
		auto changed = bytes;
		const int count = 1 + static_cast<int>(random() % 3);
		for (int edit = 0; edit < count; ++edit) {
			changed[random() % changed.size()] = static_cast<uint8_t>(random());
		}
		if (mesh.Parse(changed.data(), changed.size())) {
			// Whatever it holds, asking it things must be safe too.
			for (uint32_t layer = 0; layer < mesh.GetLayerCount(); ++layer) {
				mesh.FindPath({ 1, 1, 0 }, { 13, 13, 0 }, layer);
				mesh.WalkLine({ 1, 1, 0 }, { 13, 8, 0 }, layer);
			}
		}
	}
	CHECK(mesh.GetLayerCount() <= 32);
}

TEST_CASE("a mesh whose layer count is not the tuning's keeps its own layer radii") {
	// Nine layers, as the test and arena levels have: the seven tuning rows do not name them.
	std::vector<TestLayer> nineLayers;
	const float radii[9] = { 0.71f, 0.89f, 1.06f, 1.24f, 1.41f, 1.59f, 1.77f, 1.94f, 3.89f };
	for (const float radius : radii) {
		nineLayers.push_back(Grid(2, 2, {}, 2.f, { 0, 0 }, 0.f, radius));
	}
	const auto bytes = BuildMesh(nineLayers);
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	CHECK(mesh.GetLayerRadius(1) == doctest::Approx(0.89f));
	CHECK(mesh.GetLayerRadius(7) == doctest::Approx(1.94f));
	CHECK(mesh.LayerForRadius(1.f) == 2);
	CHECK(mesh.LayerForRadius(1.8f) == 7);
	CHECK(mesh.LayerForRadius(3.f) == 8);
}

TEST_CASE("a huge search distance still finds the mesh") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	const glm::vec3 onMesh { 1.f, 1.f, 0.f };
	CHECK(mesh.Project(onMesh, 0, 10.f).has_value());
	CHECK(mesh.Project(onMesh, 0, 1e10f).has_value());
	CHECK(mesh.Project(onMesh, 0, std::numeric_limits<float>::max()).has_value());
	// Not a distance at all: refused, as any other non-finite input.
	CHECK_FALSE(mesh.Project(onMesh, 0, std::numeric_limits<float>::infinity()).has_value());
	CHECK_FALSE(mesh.Project(glm::vec3(std::nanf(""), 1.f, 0.f), 0, 10.f).has_value());
}

TEST_CASE("the layer follows the radius the client's tuning gives") {
	const std::vector<TestLayer> sevenLayers(7, Grid(2, 2));
	const auto bytes = BuildMesh(sevenLayers);
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	SUBCASE("with the fallback radii 0.71 1.06 1.24 1.41 1.59 1.77 3.89") {
		CHECK(mesh.LayerForRadius(0.f) == 0);
		CHECK(mesh.LayerForRadius(-3.f) == 0);
		CHECK(mesh.LayerForRadius(0.71f) == 0);
		CHECK(mesh.LayerForRadius(0.72f) == 1);
		CHECK(mesh.LayerForRadius(1.06f) == 1);
		CHECK(mesh.LayerForRadius(1.5f) == 4);
		CHECK(mesh.LayerForRadius(1.77f) == 5);
		CHECK(mesh.LayerForRadius(1.78f) == 6);
		CHECK(mesh.LayerForRadius(3.89f) == 6);
		CHECK(mesh.LayerForRadius(50.f) == 6);
		CHECK(mesh.LayerForPoint() == 5);
		CHECK(mesh.GetLayerRadius(6) == doctest::Approx(3.89f));
	}
	SUBCASE("with radii read from navpowertuning.json") {
		REQUIRE(mesh.ParseTuning(R"({"source": "x", "layers": [
			{"name": "a", "radius": 0.5}, {"name": "b", "radius": 1.0}, {"name": "c", "radius": 2.5},
			{"name": "d", "radius": 3}, {"name": "e", "radius": 4}, {"name": "f", "radius": 5}, {"name": "g", "radius": 6}]})"));
		CHECK(mesh.LayerForRadius(0.5f) == 0);
		CHECK(mesh.LayerForRadius(0.9f) == 1);
		CHECK(mesh.LayerForRadius(2.f) == 2);
		CHECK(mesh.LayerForRadius(5.5f) == 6);
	}
	SUBCASE("a tuning that does not parse changes nothing") {
		CHECK_FALSE(mesh.ParseTuning("{}"));
		CHECK_FALSE(mesh.ParseTuning(R"({"layers": [{"radius": 2}, {"radius": 1}]})"));
		CHECK_FALSE(mesh.ParseTuning(R"({"layers": [{"radius": "wide"}]})"));
		CHECK_FALSE(mesh.ParseTuning(R"({"layers": [{"radius": -1}]})"));
		CHECK(mesh.LayerForRadius(1.5f) == 4);
	}
}

TEST_CASE("projection measures sideways, puts the point at the ground's height, and breaks ties by offset") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	SUBCASE("a point on the mesh stays, however high above it") {
		const auto level = mesh.Project({ 1, 1, 40 }, 0, 5);
		REQUIRE(level.has_value());
		CHECK(Near(*level, { 1, 1, 0 }));
	}
	SUBCASE("on a slope the height is the ground's") {
		const auto slope = mesh.Project({ 3, 1, 99 }, 1, 5);
		REQUIRE(slope.has_value());
		CHECK(slope->z == doctest::Approx(1.5f));
	}
	SUBCASE("a point off the mesh goes to its nearest edge, if that is near enough") {
		const auto edge = mesh.Project({ -1, 1, 0 }, 0, 2);
		REQUIRE(edge.has_value());
		CHECK(Near(*edge, { 0, 1, 0 }));
		CHECK_FALSE(mesh.Project({ -1, 1, 0 }, 0, 0.5f).has_value());
	}
	SUBCASE("a point in the missing block goes to the block's nearest side") {
		const auto side = mesh.Project({ 5, 7, 0 }, 0, 5);
		REQUIRE(side.has_value());
		CHECK(Near(*side, { 4, 7, 0 }));
	}
	SUBCASE("on the border of two polygons, the lower offset wins") {
		const auto found = mesh.Locate({ 2, 1, 0 }, 0, 1);
		REQUIRE(found.has_value());
		CHECK(found->polygon == 0);
		CHECK(found->distance == 0.f);
		const auto other = mesh.Locate({ 2, 1, 0 }, 0, 1, 1);
		CHECK(other.has_value());
	}
	SUBCASE("on top of two layers of ground, the one nearer in height wins") {
		TestLayer stacked = Grid(1, 1);
		TestLayer upper = Grid(1, 1);
		for (auto& corner : upper.polygons[0].corners) {
			corner.z = 5;
		}
		stacked.polygons.push_back(upper.polygons[0]);
		for (auto& neighbour : stacked.polygons[1].neighbours) {
			neighbour = -1;
		}
		const auto stackedBytes = BuildMesh({ stacked });
		NavMesh two;
		REQUIRE(two.Parse(stackedBytes.data(), stackedBytes.size()));
		CHECK(two.Project({ 1, 1, 4 }, 0, 1)->z == doctest::Approx(5.f));
		CHECK(two.Project({ 1, 1, 1 }, 0, 1)->z == doctest::Approx(0.f));
	}
	SUBCASE("a layer that is not there, or a point that is not a number") {
		CHECK_FALSE(mesh.Project({ 1, 1, 0 }, 5, 5).has_value());
		CHECK_FALSE(mesh.Project({ NAN, 1, 0 }, 0, 5).has_value());
		CHECK_FALSE(mesh.Project({ 1, 1, 0 }, 0, NAN).has_value());
		CHECK_FALSE(mesh.Project({ 1, 1, 0 }, 0, -1).has_value());
	}
	SUBCASE("the island of a point") {
		CHECK(mesh.GetIsland({ 1, 1, 0 }, 2, 1) != 0);
		CHECK(mesh.GetIsland({ 1, 1, 0 }, 2, 1) != mesh.GetIsland({ 101, 1, 0 }, 2, 1));
		CHECK(mesh.GetIsland({ 50, 50, 0 }, 2, 1) == 0);
	}
}

TEST_CASE("a path goes around an obstacle, and keeps to the mesh") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));
	const glm::vec3 start(1, 8, 0);
	const glm::vec3 goal(13, 8, 0);

	const auto path = mesh.FindPath(start, goal, 3);

	// The block fills x and y from 4 to 10, so the way is over its top corners.
	REQUIRE(path.size() == 4);
	CHECK(Near(path[0], start));
	CHECK(Near(path[1], { 4, 10, 0 }));
	CHECK(Near(path[2], { 10, 10, 0 }));
	CHECK(Near(path[3], goal));
	const float expected = glm::distance(glm::vec2(1, 8), glm::vec2(4, 10)) + 6.f + glm::distance(glm::vec2(10, 10), glm::vec2(13, 8));
	CHECK(NavMesh::PathLength(path) == doctest::Approx(expected).epsilon(0.001));
	CHECK(mesh.PathLength(start, goal, 3) == doctest::Approx(expected).epsilon(0.001));
	CHECK(NavMesh::PathLength(path) > glm::distance(start, goal));
	CHECK(StaysOnMesh(mesh, path, 3));

	// The same walk the other way round is the same path backwards, not a mirror of it.
	const auto back = mesh.FindPath(goal, start, 3);
	REQUIRE(back.size() == 4);
	CHECK(Near(back[1], { 10, 10, 0 }));
	CHECK(Near(back[2], { 4, 10, 0 }));
	CHECK(StaysOnMesh(mesh, back, 3));
}

TEST_CASE("a path that needs no corner is the straight line, and a path from a spot to itself is two points") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	const auto straight = mesh.FindPath({ 1, 1, 0 }, { 13, 1, 0 }, 0);
	REQUIRE(straight.size() == 2);
	CHECK(NavMesh::PathLength(straight) == doctest::Approx(12.f));

	const auto bend = mesh.FindPath({ 1, 1, 0 }, { 3, 13, 0 }, 3);
	REQUIRE(bend.size() == 2);
	CHECK(StaysOnMesh(mesh, bend, 3));

	const auto same = mesh.FindPath({ 1, 1, 0 }, { 1.5f, 1.2f, 0 }, 0);
	CHECK(same.size() == 2);
}

TEST_CASE("a path to a spot in the obstacle ends on the obstacle's edge") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	const auto path = mesh.FindPath({ 1, 8, 0 }, { 7, 7, 0 }, 3);
	REQUIRE_FALSE(path.empty());
	CHECK(StaysOnMesh(mesh, path, 3));
	CHECK(mesh.Project(path.back(), 3, 0.01f).has_value());
	CHECK_FALSE(mesh.IsReachable({ 1, 8, 0 }, { 7, 7, 0 }, 3));
}

TEST_CASE("a path on a slope has the ground's height at each end") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	const auto path = mesh.FindPath({ 1, 8, 0 }, { 13, 8, 0 }, 4);
	REQUIRE(path.size() == 4);
	CHECK(path.front().z == doctest::Approx(0.5f));
	CHECK(path.back().z == doctest::Approx(6.5f));
	CHECK(path[1].z == doctest::Approx(2.f));
	CHECK(path[2].z == doctest::Approx(5.f));
	CHECK(NavMesh::PathLength(path) > glm::distance(glm::vec3(1, 8, 0.5f), glm::vec3(13, 8, 6.5f)));
	CHECK(StaysOnMesh(mesh, path, 4));
}

TEST_CASE("there is no path between islands, and the search stops at its cap") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	SUBCASE("a goal on another island, far from the start's") {
		CHECK(mesh.FindPath({ 1, 1, 0 }, { 101, 1, 0 }, 2).empty());
		CHECK(mesh.PathLength({ 1, 1, 0 }, { 101, 1, 0 }, 2) == -1.f);
		CHECK_FALSE(mesh.IsReachable({ 1, 1, 0 }, { 101, 1, 0 }, 2));
	}
	SUBCASE("a goal on another island, nearer than the reach, goes to the start's own island") {
		const auto path = mesh.FindPath({ 1, 1, 0 }, { 101, 1, 0 }, 2, 200.f);
		REQUIRE_FALSE(path.empty());
		CHECK(path.back().x <= 4.f);
	}
	SUBCASE("a start or goal off the mesh by more than the reach") {
		CHECK(mesh.FindPath({ -50, 1, 0 }, { 13, 1, 0 }, 0).empty());
		CHECK(mesh.FindPath({ 1, 1, 0 }, { 13, 200, 0 }, 0).empty());
	}
	SUBCASE("the visit cap") {
		CHECK(mesh.FindPath({ 1, 1, 0 }, { 13, 13, 0 }, 0, 10.f, 3).empty());
		CHECK_FALSE(mesh.FindPath({ 1, 1, 0 }, { 13, 13, 0 }, 0, 10.f, 100).empty());
	}
	SUBCASE("a layer that is not there") {
		CHECK(mesh.FindPath({ 1, 1, 0 }, { 13, 1, 0 }, 7).empty());
	}
}

TEST_CASE("a spot is reachable when it is on the mesh and on the start's island") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	CHECK(mesh.IsReachable({ 1, 1, 0 }, { 13, 13, 0 }, 0));
	CHECK(mesh.IsReachable({ 1, 1, 0 }, { 13, 13, 30 }, 0));
	CHECK_FALSE(mesh.IsReachable({ 1, 1, 0 }, { 7, 7, 0 }, 0));
	CHECK_FALSE(mesh.IsReachable({ 1, 1, 0 }, { 14.5f, 7, 0 }, 0));
	CHECK(mesh.IsReachable({ 1, 1, 0 }, { 14.005f, 7, 0 }, 0));
	CHECK(mesh.IsReachable({ 1, 1, 0 }, { 14.5f, 7, 0 }, 0, 1.f));
	CHECK_FALSE(mesh.IsReachable({ -100, 1, 0 }, { 1, 1, 0 }, 0));
	CHECK(mesh.IsReachable({ 1, 1, 0 }, { 3, 1, 0 }, 2));
	CHECK_FALSE(mesh.IsReachable({ 1, 1, 0 }, { 101, 1, 0 }, 2));
}

TEST_CASE("a line walked across the mesh stops where it leaves it") {
	const auto bytes = BlockMesh();
	NavMesh mesh;
	REQUIRE(mesh.Parse(bytes.data(), bytes.size()));

	SUBCASE("into the missing block") {
		const auto walk = mesh.WalkLine({ 1, 8, 0 }, { 13, 8, 0 }, 0);
		CHECK_FALSE(walk.reachedEnd);
		CHECK(Near(walk.point, { 4, 8, 0 }));
	}
	SUBCASE("along a side, over many polygons, to its end") {
		const auto walk = mesh.WalkLine({ 1, 1, 0 }, { 13, 3, 0 }, 0);
		CHECK(walk.reachedEnd);
		CHECK(Near(walk.point, { 13, 3, 0 }));
	}
	SUBCASE("a diagonal across several polygons, to its end") {
		const auto walk = mesh.WalkLine({ 1, 1, 0 }, { 3, 13, 0 }, 0);
		CHECK(walk.reachedEnd);
		CHECK(Near(walk.point, { 3, 13, 0 }));
	}
	SUBCASE("out of the mesh by its edge") {
		const auto walk = mesh.WalkLine({ 1, 1, 0 }, { 30, 1, 0 }, 0);
		CHECK_FALSE(walk.reachedEnd);
		CHECK(Near(walk.point, { 14, 1, 0 }));
	}
	SUBCASE("on a slope the point has the ground's height") {
		const auto walk = mesh.WalkLine({ 1, 1, 0 }, { 13, 3, 0 }, 1);
		CHECK(walk.reachedEnd);
		CHECK(walk.point.z == doctest::Approx(6.5f));
	}
	SUBCASE("a line that goes nowhere, and a start that is off the mesh") {
		CHECK(mesh.WalkLine({ 1, 1, 0 }, { 1, 1, 9 }, 0).reachedEnd);
		const auto off = mesh.WalkLine({ -50, 1, 0 }, { 13, 1, 0 }, 0);
		CHECK_FALSE(off.reachedEnd);
		CHECK(off.polygon == NavMesh::noPolygon);
	}
}

TEST_CASE("zelems_1: the layers have the polygons and islands the client's file has") {
	const auto path = FindFixture({ "zelems_1.1999ae0b", "zelems_1.bfx" });
	if (path.empty()) {
		MESSAGE("skipped: no zelems_1 navmesh (extract it with scripts/navmesh_extract.py)");
		return;
	}

	NavMesh mesh;
	REQUIRE(mesh.Load(path));
	REQUIRE(mesh.GetLayerCount() == 7);

	const size_t polygons[7] = { 569, 565, 566, 448, 448, 451, 463 };
	for (uint32_t layer = 0; layer < 7; ++layer) {
		CHECK(mesh.GetPolygonCount(layer) == polygons[layer]);
		CHECK(mesh.GetIslandCount(layer) == (layer == 6 ? 6u : 5u));
	}
	CHECK(mesh.LayerForPoint() == 5);
	CHECK(mesh.LayerForRadius(1.0f) == 1);
	CHECK(mesh.LayerForRadius(1.2f) == 2);
}

TEST_CASE("zelems_1: paths have the length of the verified prototype's") {
	const auto file = FindFixture({ "zelems_1.1999ae0b", "zelems_1.bfx" });
	if (file.empty()) {
		MESSAGE("skipped: no zelems_1 navmesh (extract it with scripts/navmesh_extract.py)");
		return;
	}

	NavMesh mesh;
	REQUIRE(mesh.Load(file));

	// research/sync/nav/walks_layer.txt: creatures' walks from the traces, planned by nav/bfx.py with
	// the funnel's left and right the right way round.
	struct Walk {
		uint32_t layer;
		glm::vec3 from;
		glm::vec3 to;
		float length;
		size_t points;
	};
	const Walk walks[] = {
		{ 2, { -170.1f, 79.7f, 0 }, { -179.1f, 80.3f, 0 }, 8.4f, 2 },
		{ 2, { -185.1f, 79.3f, 0 }, { -182.9f, 74.5f, 0 }, 3.7f, 2 },
		{ 2, { -174.9f, 70.6f, 0 }, { -167.4f, 73.7f, 0 }, 12.3f, 5 },
		{ 2, { -167.4f, 73.7f, 0 }, { -172.3f, 81.9f, 0 }, 8.3f, 2 },
		{ 1, { -162.8f, -77.9f, 0 }, { -160.5f, -84.0f, 0 }, 5.4f, 2 },
		{ 1, { -160.5f, -84.0f, 0 }, { -151.5f, -85.9f, 0 }, 11.0f, 5 },
		{ 1, { -176.0f, -35.1f, 0 }, { -184.5f, -59.3f, 0 }, 25.3f, 2 },
		{ 1, { -184.5f, -59.3f, 0 }, { -164.6f, -66.7f, 0 }, 28.0f, 6 },
		{ 1, { -189.5f, -50.5f, 0 }, { -175.5f, -66.7f, 0 }, 25.4f, 6 },
		{ 1, { -188.7f, -48.6f, 0 }, { -173.8f, -63.0f, 0 }, 20.8f, 4 },
	};
	for (const Walk& walk : walks) {
		CAPTURE(walk.layer);
		CAPTURE(walk.from.x);
		CAPTURE(walk.from.y);
		const auto path = mesh.FindPath(walk.from, walk.to, walk.layer);
		REQUIRE_FALSE(path.empty());
		CHECK(NavMesh::PathLength(path) == doctest::Approx(walk.length).epsilon(0.02));
		CHECK(path.size() == walk.points);
		CHECK(StaysOnMesh(mesh, path, walk.layer));
		// Never shorter than the straight line between its ends.
		CHECK(NavMesh::PathLength(path) >= glm::distance(glm::vec2(path.front()), glm::vec2(path.back())) - 0.001f);
		CHECK(mesh.IsReachable(walk.from, path.back(), walk.layer));
	}
}

TEST_CASE("zelems_1: every path between spread out spots is on the mesh") {
	const auto file = FindFixture({ "zelems_1.1999ae0b", "zelems_1.bfx" });
	if (file.empty()) {
		MESSAGE("skipped: no zelems_1 navmesh (extract it with scripts/navmesh_extract.py)");
		return;
	}

	NavMesh mesh;
	REQUIRE(mesh.Load(file));
	const uint32_t layer = mesh.LayerForRadius(1.2f);

	// Spots on the first island's ground: the walks above, which the client's creatures stood on.
	const glm::vec3 spots[] = {
		{ -170.1f, 79.7f, 0 }, { -179.1f, 80.3f, 0 }, { -174.9f, 70.6f, 0 }, { -167.4f, 73.7f, 0 },
		{ -160.5f, -84.f, 0 }, { -176.f, -35.1f, 0 }, { -184.5f, -59.3f, 0 }, { -164.6f, -66.7f, 0 },
		{ -189.5f, -50.5f, 0 }, { -173.8f, -63.f, 0 } };
	int found = 0;
	for (const glm::vec3& from : spots) {
		for (const glm::vec3& to : spots) {
			const auto path = mesh.FindPath(from, to, layer);
			if (path.empty()) {
				continue;
			}
			++found;
			REQUIRE(StaysOnMesh(mesh, path, layer));
		}
	}
	CHECK(found > 50);

	// A line walked from a spot toward another ends on the mesh.
	const auto walk = mesh.WalkLine(spots[0], spots[6], layer);
	CHECK(mesh.Locate(walk.point, layer, 0.05f).has_value());
}
