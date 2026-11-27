
// Include
#include "navmesh.h"

// A port of the navigation package of darkspin (github.com/darkspinnet/darkspin, MIT license):
// the .bfx reader, projection, island labelling and the search on the stored edge costs. The
// funnel is from research/sync/nav/bfx.py. See LICENSES.md.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <queue>
#include <sstream>
#include <utility>

// Game
namespace Game {
	namespace {
		// The file, from notes/client/navmesh.md of darkspin and checked against the 5.3.0.103 meshes.
		// An envelope (0, 2, size - 0x18), an image (0x10000, size - 0x24) and then a header per layer.
		constexpr uint64_t envelopeSize = 0x18;
		constexpr uint64_t layersOffset = 0x30;
		constexpr uint64_t layerHeaderSize = 0x13C;
		constexpr uint64_t polygonPrefixSize = 0x34;
		constexpr uint64_t edgeSize = 0x18;
		constexpr uint64_t spatialPrefixSize = 0x1C;
		constexpr uint32_t imageVersion = 0x10000;
		constexpr uint32_t imageHeaderSize = 0x1C;
		constexpr uint32_t maxLayers = 32;
		constexpr uint32_t maxEdges = 127;
		constexpr uint32_t dynamicBit = 30;

		// The lookup grid, in metres.
		constexpr float cellSize = 8.f;
		constexpr uint64_t maxCellsPerPolygon = 1u << 20;
		// And in all, per polygon: real meshes cover a few cells each. Damaged data with many large,
		// overlapping polygons would otherwise grow the grid until memory runs out.
		constexpr uint64_t maxCellEntriesPerPolygon = 1024;

		// A query box covering more cells than this is answered by looking at every polygon instead.
		constexpr int64_t maxQueryCells = 4096;

		// How far outside an edge a point may be and still count as inside it, and the least a polygon's
		// corners may turn the wrong way (squared metres, sideways).
		constexpr float turnTolerance = 1e-4f;

		// The least height of a polygon's normal for the height at a point to come from its plane.
		constexpr float flatNormal = 1e-6f;

		uint32_t ReadU32(const uint8_t* data, uint64_t offset) {
			return static_cast<uint32_t>(data[offset])
				| static_cast<uint32_t>(data[offset + 1]) << 8
				| static_cast<uint32_t>(data[offset + 2]) << 16
				| static_cast<uint32_t>(data[offset + 3]) << 24;
		}

		float ReadF32(const uint8_t* data, uint64_t offset) {
			const uint32_t bits = ReadU32(data, offset);
			float value;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}

		glm::vec3 ReadVec3(const uint8_t* data, uint64_t offset) {
			return { ReadF32(data, offset), ReadF32(data, offset + 4), ReadF32(data, offset + 8) };
		}

		bool HasRange(uint64_t size, uint64_t offset, uint64_t length) {
			return offset <= size && length <= size - offset;
		}

		bool IsFinite(float value) {
			return std::isfinite(value);
		}

		bool IsFinite(const glm::vec3& value) {
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		}

		bool IsFinitePositive(float value) {
			return value > 0 && IsFinite(value);
		}

		float Cross2(const glm::vec2& a, const glm::vec2& b) {
			return a.x * b.y - a.y * b.x;
		}

		// The funnel's signed area: positive when c is to the right of a to b.
		float Area2(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
			return (c.x - a.x) * (b.y - a.y) - (b.x - a.x) * (c.y - a.y);
		}

		glm::vec2 Flat(const glm::vec3& value) {
			return { value.x, value.y };
		}

		glm::vec2 ClosestOnSegment(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
			const glm::vec2 edge = b - a;
			const float lengthSquared = glm::dot(edge, edge);
			if (lengthSquared <= 0) {
				return a;
			}
			const float fraction = std::clamp(glm::dot(point - a, edge) / lengthSquared, 0.f, 1.f);
			return a + edge * fraction;
		}

		bool CellOf(float x, float y, int32_t& cellX, int32_t& cellY) {
			const double fx = std::floor(static_cast<double>(x) / cellSize);
			const double fy = std::floor(static_cast<double>(y) / cellSize);
			if (!(fx > -1e9 && fx < 1e9 && fy > -1e9 && fy < 1e9)) {
				return false;
			}
			cellX = static_cast<int32_t>(fx);
			cellY = static_cast<int32_t>(fy);
			return true;
		}

		uint64_t CellKey(int32_t cellX, int32_t cellY) {
			return static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32 | static_cast<uint32_t>(cellY);
		}

		std::string Hex(uint64_t value) {
			std::ostringstream stream;
			stream << std::hex << "0x" << value;
			return stream.str();
		}
	}

	// Loading
	bool NavMesh::Load(const std::filesystem::path& path) {
		Clear();

		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			std::cout << "NavMesh: Could not open '" << path.string() << "'." << std::endl;
			return false;
		}

		std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		if (!Parse(data.data(), data.size())) {
			std::cout << "NavMesh: Could not load '" << path.string() << "'." << std::endl;
			return false;
		}

		const auto tuning = path.parent_path() / "navpowertuning.json";
		std::error_code error;
		if (std::filesystem::is_regular_file(tuning, error)) {
			LoadTuning(tuning);
		}
		return true;
	}

	bool NavMesh::Parse(const uint8_t* data, size_t size) {
		Clear();

		std::string error;
		std::vector<Layer> layers;
		auto fail = [&](const std::string& message) {
			std::cout << "NavMesh: " << message << "." << std::endl;
			return false;
		};

		if (data == nullptr || size < layersOffset) {
			return fail("the file is too short");
		}
		if (ReadU32(data, 0) != 0 || ReadU32(data, 4) != 2 || ReadU32(data, 8) != size - envelopeSize
			|| ReadU32(data, 0x10) != 0 || ReadU32(data, 0x14) != 0) {
			return fail("the envelope is wrong");
		}
		if (ReadU32(data, 0x18) != imageVersion || ReadU32(data, 0x1C) != size - 0x24
			|| ReadU32(data, 0x20) != 0 || ReadU32(data, 0x24) != 0 || ReadU32(data, 0x28) != imageHeaderSize) {
			return fail("the image header is wrong");
		}

		const uint32_t layerCount = ReadU32(data, 0x2C);
		if (layerCount == 0 || layerCount > maxLayers) {
			return fail("the layer count " + std::to_string(layerCount) + " is wrong");
		}

		layers.resize(layerCount);
		uint64_t offset = layersOffset;
		for (uint32_t index = 0; index < layerCount; ++index) {
			if (!ParseLayer(data, size, offset, index, layers[index], offset, error)) {
				return fail("layer " + std::to_string(index) + ": " + error);
			}
		}
		if (offset != size) {
			return fail("data follows the last layer");
		}

		mLayers = std::move(layers);
		return true;
	}

	void NavMesh::Clear() {
		mLayers.clear();
	}

	bool NavMesh::ParseLayer(const uint8_t* data, uint64_t size, uint64_t offset, uint32_t index, Layer& layer,
		uint64_t& next, std::string& error) {
		if (!HasRange(size, offset, layerHeaderSize)) {
			error = "the header is cut short";
			return false;
		}
		if (ReadU32(data, offset) != imageHeaderSize || ReadU32(data, offset + 4) != index) {
			error = "the header is not this layer's";
			return false;
		}

		const uint64_t arenaSize = ReadU32(data, offset + 8);
		const uint64_t layerSize = ReadU32(data, offset + 0x0C);
		if (layerSize < layerHeaderSize + spatialPrefixSize || !HasRange(size, offset, layerSize)
			|| arenaSize > layerSize - layerHeaderSize - spatialPrefixSize) {
			error = "the size is wrong";
			return false;
		}

		layer.radius = ReadF32(data, offset + 0x18);
		const glm::vec3 boundsMin = ReadVec3(data, offset + 0x24);
		const glm::vec3 boundsMax = ReadVec3(data, offset + 0x30);
		if (!IsFinitePositive(ReadF32(data, offset + 0x10)) || !IsFinitePositive(ReadF32(data, offset + 0x14))
			|| !IsFinitePositive(layer.radius) || !IsFinitePositive(ReadF32(data, offset + 0x1C))
			|| !IsFinitePositive(ReadF32(data, offset + 0x20)) || !IsFinite(boundsMin) || !IsFinite(boundsMax)
			|| boundsMin.x > boundsMax.x || boundsMin.y > boundsMax.y || boundsMin.z > boundsMax.z) {
			error = "the header holds bad numbers";
			return false;
		}

		const uint64_t arenaStart = offset + layerHeaderSize;
		const uint64_t arenaEnd = arenaStart + arenaSize;
		std::vector<uint32_t> neighbourOffsets;
		uint64_t cursor = arenaStart;
		while (cursor < arenaEnd) {
			const std::string where = "polygon " + Hex(cursor - offset) + ": ";
			if (!HasRange(arenaEnd, cursor, polygonPrefixSize)) {
				error = where + "the record is cut short";
				return false;
			}
			for (uint64_t at = 0; at < 0x10; at += 4) {
				if (ReadU32(data, cursor + at) != 0) {
					error = where + "the runtime prefix is not empty";
					return false;
				}
			}

			const glm::vec3 center = ReadVec3(data, cursor + 0x10);
			const float radius = ReadF32(data, cursor + 0x1C);
			const uint32_t meta0 = ReadU32(data, cursor + 0x28);
			const uint32_t edgeCount = meta0 & 0x7F;
			if (edgeCount < 3 || edgeCount > maxEdges || ((meta0 >> 23) & 0x1F) != index || ((meta0 >> dynamicBit) & 1)
				|| !IsFinite(center) || !IsFinite(radius) || radius < 0) {
				error = where + "the header is wrong";
				return false;
			}

			const uint64_t polygonSize = polygonPrefixSize + edgeCount * edgeSize;
			if (!HasRange(arenaEnd, cursor, polygonSize)) {
				error = where + "the edges are cut short";
				return false;
			}

			Polygon polygon;
			polygon.offset = static_cast<uint32_t>(cursor - offset);
			polygon.first = static_cast<uint32_t>(layer.vertices.size());
			polygon.count = edgeCount;
			for (uint32_t edge = 0; edge < edgeCount; ++edge) {
				const uint64_t at = cursor + polygonPrefixSize + edge * edgeSize;
				Vertex vertex;
				vertex.position = ReadVec3(data, at + 4);
				vertex.cost = ReadU32(data, at + 0x14);
				const uint32_t neighbour = ReadU32(data, at);
				if (!IsFinite(vertex.position) || (neighbour == 0) != (vertex.cost == 0)) {
					error = where + "an edge is wrong";
					return false;
				}
				layer.vertices.push_back(vertex);
				neighbourOffsets.push_back(neighbour);
			}

			// Bounds, the plane (Newell's normal) and the winding, sideways: counter-clockwise and convex.
			glm::vec3 normal {};
			glm::vec3 sum {};
			polygon.boundsMin = polygon.boundsMax = layer.vertices[polygon.first].position;
			const glm::vec2 origin = Flat(layer.vertices[polygon.first].position);
			double area = 0; // from the first corner on, so a sliver far from the origin does not cancel to nothing
			for (uint32_t edge = 0; edge < edgeCount; ++edge) {
				const glm::vec3& a = layer.vertices[polygon.first + edge].position;
				const glm::vec3& b = layer.vertices[polygon.first + (edge + 1) % edgeCount].position;
				const glm::vec3& c = layer.vertices[polygon.first + (edge + 2) % edgeCount].position;
				normal.x += (a.y - b.y) * (a.z + b.z);
				normal.y += (a.z - b.z) * (a.x + b.x);
				normal.z += (a.x - b.x) * (a.y + b.y);
				sum += a;
				area += static_cast<double>(Cross2(Flat(a) - origin, Flat(b) - origin));
				polygon.boundsMin = glm::min(polygon.boundsMin, a);
				polygon.boundsMax = glm::max(polygon.boundsMax, a);
				if (Cross2(Flat(b) - Flat(a), Flat(c) - Flat(b)) < -turnTolerance) {
					error = where + "the polygon is not convex and counter-clockwise";
					return false;
				}
			}
			const float normalLength = glm::length(normal);
			if (!(area > 0) || !(normalLength > 0) || !IsFinite(normalLength)) {
				error = where + "the polygon has no area, or winds clockwise";
				return false;
			}
			polygon.normal = normal / normalLength;
			polygon.flatZ = sum.z / static_cast<float>(edgeCount);
			polygon.planeD = glm::dot(polygon.normal, sum / static_cast<float>(edgeCount));

			layer.polygons.push_back(polygon);
			cursor += polygonSize;
		}
		if (cursor != arenaEnd || layer.polygons.empty()) {
			error = "the polygon arena is empty or does not end with a polygon";
			return false;
		}

		// Neighbour offsets are from the start of the layer and name another polygon of it.
		for (Polygon& polygon : layer.polygons) {
			for (uint32_t edge = 0; edge < polygon.count; ++edge) {
				const uint32_t stored = neighbourOffsets[polygon.first + edge];
				if (stored == 0) {
					continue;
				}
				const auto found = std::lower_bound(layer.polygons.begin(), layer.polygons.end(), stored,
					[](const Polygon& candidate, uint32_t value) { return candidate.offset < value; });
				if (found == layer.polygons.end() || found->offset != stored || &*found == &polygon) {
					error = "polygon " + Hex(polygon.offset) + ": the neighbour " + Hex(stored) + " is not another polygon";
					return false;
				}
				layer.vertices[polygon.first + edge].neighbour = static_cast<uint32_t>(found - layer.polygons.begin());
			}
		}
		if (!LinkPolygons(layer, error)) {
			return false;
		}

		// What follows the arena: the layer's bounds and the size of the search tree the client keeps.
		if (!HasRange(size, arenaEnd, spatialPrefixSize)) {
			error = "the spatial prefix is cut short";
			return false;
		}
		const glm::vec3 spatialMin = ReadVec3(data, arenaEnd);
		const glm::vec3 spatialMax = ReadVec3(data, arenaEnd + 0x0C);
		const uint64_t treeSize = ReadU32(data, arenaEnd + 0x18);
		if (!IsFinite(spatialMin) || !IsFinite(spatialMax) || spatialMin.x > spatialMax.x || spatialMin.y > spatialMax.y
			|| spatialMin.z > spatialMax.z || layerHeaderSize + arenaSize + spatialPrefixSize + treeSize != layerSize) {
			error = "the spatial part is wrong";
			return false;
		}

		if (!BuildGrid(layer, error)) {
			return false;
		}

		next = offset + layerSize;
		return true;
	}

	bool NavMesh::LinkPolygons(Layer& layer, std::string& error) {
		// Every link must have its reverse: the neighbour has exactly one edge back, over the same two vertices.
		for (uint32_t index = 0; index < layer.polygons.size(); ++index) {
			const Polygon& polygon = layer.polygons[index];
			for (uint32_t edge = 0; edge < polygon.count; ++edge) {
				const Vertex& from = layer.vertices[polygon.first + edge];
				if (from.neighbour == noPolygon) {
					continue;
				}
				const glm::vec3& to = layer.vertices[polygon.first + (edge + 1) % polygon.count].position;
				const Polygon& other = layer.polygons[from.neighbour];
				uint32_t back = 0;
				for (uint32_t otherEdge = 0; otherEdge < other.count; ++otherEdge) {
					const Vertex& candidate = layer.vertices[other.first + otherEdge];
					const glm::vec3& candidateTo = layer.vertices[other.first + (otherEdge + 1) % other.count].position;
					if (candidate.neighbour == index && candidate.position == to && candidateTo == from.position) {
						++back;
					}
				}
				if (back != 1) {
					error = "polygon " + Hex(polygon.offset) + ": the link through edge " + std::to_string(edge)
						+ " has no reverse";
					return false;
				}
			}
		}

		// Islands: the connected parts, numbered from 1.
		std::vector<uint32_t> stack;
		for (uint32_t start = 0; start < layer.polygons.size(); ++start) {
			if (layer.polygons[start].island != 0) {
				continue;
			}
			const uint32_t island = ++layer.islands;
			layer.polygons[start].island = island;
			stack.push_back(start);
			while (!stack.empty()) {
				const uint32_t current = stack.back();
				stack.pop_back();
				const Polygon& polygon = layer.polygons[current];
				for (uint32_t edge = 0; edge < polygon.count; ++edge) {
					const uint32_t neighbour = layer.vertices[polygon.first + edge].neighbour;
					if (neighbour != noPolygon && layer.polygons[neighbour].island == 0) {
						layer.polygons[neighbour].island = island;
						stack.push_back(neighbour);
					}
				}
			}
		}
		return true;
	}

	bool NavMesh::BuildGrid(Layer& layer, std::string& error) {
		layer.minCellX = layer.minCellY = std::numeric_limits<int32_t>::max();
		layer.maxCellX = layer.maxCellY = std::numeric_limits<int32_t>::min();
		const uint64_t maxEntries = maxCellEntriesPerPolygon * std::max<uint64_t>(layer.polygons.size(), 1);
		uint64_t entries = 0;
		for (uint32_t index = 0; index < layer.polygons.size(); ++index) {
			const Polygon& polygon = layer.polygons[index];
			int32_t x0, y0, x1, y1;
			if (!CellOf(polygon.boundsMin.x, polygon.boundsMin.y, x0, y0) || !CellOf(polygon.boundsMax.x, polygon.boundsMax.y, x1, y1)) {
				error = "polygon " + Hex(polygon.offset) + " is too far out";
				return false;
			}
			const uint64_t cells = (static_cast<uint64_t>(static_cast<int64_t>(x1) - x0) + 1)
				* (static_cast<uint64_t>(static_cast<int64_t>(y1) - y0) + 1);
			if (cells > maxCellsPerPolygon) {
				error = "polygon " + Hex(polygon.offset) + " is too large";
				return false;
			}
			entries += cells;
			if (entries > maxEntries) {
				error = "the polygons cover too many grid cells";
				return false;
			}
			layer.minCellX = std::min(layer.minCellX, x0);
			layer.minCellY = std::min(layer.minCellY, y0);
			layer.maxCellX = std::max(layer.maxCellX, x1);
			layer.maxCellY = std::max(layer.maxCellY, y1);
			for (int32_t x = x0; x <= x1; ++x) {
				for (int32_t y = y0; y <= y1; ++y) {
					layer.grid[CellKey(x, y)].push_back(index);
				}
			}
		}
		return true;
	}

	// Tuning
	bool NavMesh::LoadTuning(const std::filesystem::path& path) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			std::cout << "NavMesh: Could not open '" << path.string() << "'." << std::endl;
			return false;
		}

		const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		if (!ParseTuning(text)) {
			std::cout << "NavMesh: Could not read the layer radii in '" << path.string() << "'." << std::endl;
			return false;
		}
		return true;
	}

	bool NavMesh::ParseTuning(const std::string& text) {
		// navmesh_extract.py writes {"layers": [{"name": ..., "radius": 0.71, ...}, ...]}. Only the radii matter.
		static const std::string key = "\"radius\"";
		std::vector<float> radii;
		for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + key.size())) {
			size_t colon = text.find_first_not_of(" \t\r\n", at + key.size());
			if (colon == std::string::npos || text[colon] != ':') {
				return false;
			}
			const char* begin = text.c_str() + colon + 1;
			char* end = nullptr;
			const float radius = std::strtof(begin, &end);
			if (end == begin || !IsFinitePositive(radius) || (!radii.empty() && radius <= radii.back())) {
				return false;
			}
			radii.push_back(radius);
		}
		if (radii.empty() || radii.size() > maxLayers) {
			return false;
		}
		mRadii = std::move(radii);
		return true;
	}

	void NavMesh::SetLayerRadii(std::vector<float> radii) {
		mRadii = std::move(radii);
	}

	// Layers
	bool NavMesh::IsLoaded() const {
		return !mLayers.empty();
	}

	size_t NavMesh::GetLayerCount() const {
		return mLayers.size();
	}

	size_t NavMesh::GetPolygonCount(uint32_t layer) const {
		return layer < mLayers.size() ? mLayers[layer].polygons.size() : 0;
	}

	uint32_t NavMesh::GetIslandCount(uint32_t layer) const {
		return layer < mLayers.size() ? mLayers[layer].islands : 0;
	}

	float NavMesh::GetLayerRadius(uint32_t layer) const {
		// The tuning's rows name the layers of a mesh built with them, so only a mesh with as many
		// layers takes them; others (9 layers on the test and arena levels) keep their own radius.
		if (mRadii.size() == mLayers.size() && layer < mRadii.size()) {
			return mRadii[layer];
		}
		return layer < mLayers.size() ? mLayers[layer].radius : 0.f;
	}

	uint32_t NavMesh::LayerForRadius(float radius) const {
		if (mLayers.empty()) {
			return 0;
		}

		radius = std::max(radius, 0.1f);
		for (uint32_t layer = 0; layer < mLayers.size(); ++layer) {
			if (GetLayerRadius(layer) >= radius) {
				return layer;
			}
		}
		return static_cast<uint32_t>(mLayers.size() - 1);
	}

	uint32_t NavMesh::LayerForPoint() const {
		return mLayers.size() > 2 ? static_cast<uint32_t>(mLayers.size() - 2) : 0;
	}

	// Projection
	float NavMesh::HeightAt(const Polygon& polygon, float x, float y) const {
		if (polygon.normal.z > flatNormal) {
			return (polygon.planeD - polygon.normal.x * x - polygon.normal.y * y) / polygon.normal.z;
		}
		return polygon.flatZ;
	}

	void NavMesh::Candidates(const Layer& layer, const glm::vec3& point, float maxDistance, std::vector<uint32_t>& out) const {
		out.clear();

		if (layer.polygons.empty() || !std::isfinite(point.x) || !std::isfinite(point.y) || std::isnan(maxDistance)) {
			return;
		}
		// Clamped to the layer before it becomes a cell, so a huge distance still finds every polygon.
		const double reach = std::max(0.0, static_cast<double>(maxDistance));
		const auto cell = [](double value, int32_t minCell, int32_t maxCell) {
			const double floored = std::floor(value / cellSize);
			return static_cast<int32_t>(std::clamp(floored, static_cast<double>(minCell) - 1.0, static_cast<double>(maxCell) + 1.0));
		};
		int32_t x0 = cell(point.x - reach, layer.minCellX, layer.maxCellX);
		int32_t y0 = cell(point.y - reach, layer.minCellY, layer.maxCellY);
		int32_t x1 = cell(point.x + reach, layer.minCellX, layer.maxCellX);
		int32_t y1 = cell(point.y + reach, layer.minCellY, layer.maxCellY);
		x0 = std::max(x0, layer.minCellX);
		y0 = std::max(y0, layer.minCellY);
		x1 = std::min(x1, layer.maxCellX);
		y1 = std::min(y1, layer.maxCellY);
		if (x0 > x1 || y0 > y1) {
			return;
		}

		const int64_t cells = (static_cast<int64_t>(x1) - x0 + 1) * (static_cast<int64_t>(y1) - y0 + 1);
		if (cells > maxQueryCells) {
			out.resize(layer.polygons.size());
			for (uint32_t index = 0; index < out.size(); ++index) {
				out[index] = index;
			}
			return;
		}

		for (int32_t x = x0; x <= x1; ++x) {
			for (int32_t y = y0; y <= y1; ++y) {
				const auto found = layer.grid.find(CellKey(x, y));
				if (found != layer.grid.end()) {
					out.insert(out.end(), found->second.begin(), found->second.end());
				}
			}
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
	}

	std::optional<NavMesh::Projection> NavMesh::Locate(const glm::vec3& point, uint32_t layerIndex, float maxDistance,
		uint32_t island) const {
		if (layerIndex >= mLayers.size() || !IsFinite(point) || !IsFinite(maxDistance) || maxDistance < 0) {
			return std::nullopt;
		}

		const Layer& layer = mLayers[layerIndex];
		std::vector<uint32_t> candidates;
		Candidates(layer, point, maxDistance, candidates);

		const glm::vec2 flat = Flat(point);
		const float maxSquared = maxDistance * maxDistance;
		float bestSquared = std::numeric_limits<float>::infinity();
		float bestHeight = std::numeric_limits<float>::infinity();
		std::optional<Projection> best;
		for (const uint32_t index : candidates) {
			const Polygon& polygon = layer.polygons[index];
			if (island != 0 && polygon.island != island) {
				continue;
			}
			if (flat.x < polygon.boundsMin.x - maxDistance || flat.x > polygon.boundsMax.x + maxDistance
				|| flat.y < polygon.boundsMin.y - maxDistance || flat.y > polygon.boundsMax.y + maxDistance) {
				continue;
			}

			// The closest spot of a convex polygon, sideways: the point itself if it is inside every edge,
			// else the closest spot of an edge.
			glm::vec2 closest = flat;
			bool inside = true;
			float closestSquared = std::numeric_limits<float>::infinity();
			for (uint32_t edge = 0; edge < polygon.count; ++edge) {
				const glm::vec2 a = Flat(layer.vertices[polygon.first + edge].position);
				const glm::vec2 b = Flat(layer.vertices[polygon.first + (edge + 1) % polygon.count].position);
				if (Cross2(b - a, flat - a) < 0) {
					inside = false;
				}
				const glm::vec2 candidate = ClosestOnSegment(flat, a, b);
				const glm::vec2 delta = flat - candidate;
				const float squared = glm::dot(delta, delta);
				if (squared < closestSquared) {
					closestSquared = squared;
					closest = candidate;
				}
			}
			if (inside) {
				closest = flat;
				closestSquared = 0;
			}

			if (closestSquared > maxSquared) {
				continue;
			}

			const float z = HeightAt(polygon, closest.x, closest.y);
			const float height = std::abs(point.z - z);
			if (best) {
				if (closestSquared > bestSquared) {
					continue;
				}
				if (closestSquared == bestSquared) {
					if (height > bestHeight || (height == bestHeight && polygon.offset >= layer.polygons[best->polygon].offset)) {
						continue;
					}
				}
			}

			bestSquared = closestSquared;
			bestHeight = height;
			best = Projection { { closest.x, closest.y, z }, index, polygon.island, std::sqrt(closestSquared) };
		}
		return best;
	}

	std::optional<glm::vec3> NavMesh::Project(const glm::vec3& point, uint32_t layer, float maxDistance) const {
		const auto found = Locate(point, layer, maxDistance);
		if (!found) {
			return std::nullopt;
		}
		return found->position;
	}

	uint32_t NavMesh::GetIsland(const glm::vec3& point, uint32_t layer, float maxDistance) const {
		const auto found = Locate(point, layer, maxDistance);
		return found ? found->island : 0;
	}

	// Paths
	bool NavMesh::Corridor(const Layer& layer, uint32_t from, uint32_t to, size_t maxVisited,
		std::vector<uint32_t>& polygons, std::vector<uint32_t>& edges) const {
		// Dijkstra on the edge costs the file stores (about 500 per metre between polygon centres).
		// Ties go to the lower polygon, so the same mesh always gives the same corridor.
		using Entry = std::pair<uint64_t, uint32_t>;
		constexpr uint64_t unreached = std::numeric_limits<uint64_t>::max();
		std::vector<uint64_t> cost(layer.polygons.size(), unreached);
		std::vector<uint32_t> previous(layer.polygons.size(), noPolygon);
		std::vector<uint32_t> previousEdge(layer.polygons.size(), 0);
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;

		cost[from] = 0;
		queue.push({ 0, from });
		size_t visited = 0;
		bool found = false;
		while (!queue.empty() && visited < maxVisited) {
			const auto [current, index] = queue.top();
			queue.pop();
			if (current != cost[index]) {
				continue;
			}
			++visited;
			if (index == to) {
				found = true;
				break;
			}

			const Polygon& polygon = layer.polygons[index];
			for (uint32_t edge = 0; edge < polygon.count; ++edge) {
				const Vertex& vertex = layer.vertices[polygon.first + edge];
				if (vertex.neighbour == noPolygon) {
					continue;
				}
				const uint64_t candidate = current + vertex.cost;
				if (candidate >= cost[vertex.neighbour]) {
					continue;
				}
				cost[vertex.neighbour] = candidate;
				previous[vertex.neighbour] = index;
				previousEdge[vertex.neighbour] = edge;
				queue.push({ candidate, vertex.neighbour });
			}
		}
		if (!found) {
			return false;
		}

		// polygons: from..to; edges[i] leaves polygons[i] for polygons[i + 1].
		polygons.clear();
		edges.clear();
		for (uint32_t index = to; index != from; index = previous[index]) {
			polygons.push_back(index);
			edges.push_back(previousEdge[index]);
		}
		polygons.push_back(from);
		std::reverse(polygons.begin(), polygons.end());
		std::reverse(edges.begin(), edges.end());
		return true;
	}

	std::vector<glm::vec3> NavMesh::Funnel(const Layer& layer, const glm::vec3& start, const glm::vec3& goal,
		const std::vector<uint32_t>& polygons, const std::vector<uint32_t>& edges) const {
		// Leaving a polygon (counter-clockwise from +z) over the edge from vertex i to vertex i + 1, the
		// left side is vertex i + 1 and the right side vertex i. The first draft had them the other way
		// round, which made paths wind around corners that were not there.
		std::vector<Portal> portals;
		portals.reserve(edges.size() + 2);
		portals.push_back({ start, start });
		for (size_t index = 0; index < edges.size(); ++index) {
			const Polygon& polygon = layer.polygons[polygons[index]];
			portals.push_back({
				layer.vertices[polygon.first + (edges[index] + 1) % polygon.count].position,
				layer.vertices[polygon.first + edges[index]].position });
		}
		portals.push_back({ goal, goal });

		std::vector<glm::vec3> points { start };
		glm::vec3 apex = start;
		glm::vec3 left = start;
		glm::vec3 right = start;
		size_t apexIndex = 0;
		size_t leftIndex = 0;
		size_t rightIndex = 0;

		// Each restart moves the apex forward, so this is more than enough; it only stops a degenerate
		// portal from circling.
		size_t budget = portals.size() * portals.size() + 64;
		size_t index = 1;
		bool stuck = false;
		while (index < portals.size()) {
			if (budget-- == 0) {
				stuck = true;
				break;
			}

			const glm::vec3& portalLeft = portals[index].left;
			const glm::vec3& portalRight = portals[index].right;
			if (Area2(apex, right, portalRight) <= 0.f) {
				if (apex == right || Area2(apex, left, portalRight) > 0.f) {
					right = portalRight;
					rightIndex = index;
				} else {
					points.push_back(left);
					apex = left;
					apexIndex = leftIndex;
					left = right = apex;
					leftIndex = rightIndex = apexIndex;
					index = apexIndex + 1;
					continue;
				}
			}
			if (Area2(apex, left, portalLeft) >= 0.f) {
				if (apex == left || Area2(apex, right, portalLeft) < 0.f) {
					left = portalLeft;
					leftIndex = index;
				} else {
					points.push_back(right);
					apex = right;
					apexIndex = rightIndex;
					left = right = apex;
					leftIndex = rightIndex = apexIndex;
					index = apexIndex + 1;
					continue;
				}
			}
			++index;
		}

		if (stuck) {
			// The middle of every portal stays inside the corridor.
			points.assign(1, start);
			for (size_t portal = 1; portal + 1 < portals.size(); ++portal) {
				points.push_back((portals[portal].left + portals[portal].right) * 0.5f);
			}
		}
		points.push_back(goal);

		// A corner can fall on the point before it.
		points.erase(std::unique(points.begin(), points.end(), [](const glm::vec3& a, const glm::vec3& b) {
			return a.x == b.x && a.y == b.y;
		}), points.end());
		if (points.size() < 2) {
			points.push_back(goal);
		}
		return points;
	}

	std::vector<glm::vec3> NavMesh::FindPath(const glm::vec3& start, const glm::vec3& goal, uint32_t layerIndex,
		float maxProject, size_t maxVisited) const {
		const auto from = Locate(start, layerIndex, maxProject);
		if (!from) {
			return {};
		}
		const auto to = Locate(goal, layerIndex, maxProject, from->island);
		if (!to) {
			return {};
		}
		if (from->polygon == to->polygon) {
			return { from->position, to->position };
		}

		const Layer& layer = mLayers[layerIndex];
		std::vector<uint32_t> polygons;
		std::vector<uint32_t> edges;
		if (!Corridor(layer, from->polygon, to->polygon, maxVisited, polygons, edges)) {
			return {};
		}
		return Funnel(layer, from->position, to->position, polygons, edges);
	}

	float NavMesh::PathLength(const std::vector<glm::vec3>& path) {
		float length = 0;
		for (size_t index = 1; index < path.size(); ++index) {
			length += glm::distance(path[index - 1], path[index]);
		}
		return length;
	}

	float NavMesh::PathLength(const glm::vec3& start, const glm::vec3& goal, uint32_t layer) const {
		const auto path = FindPath(start, goal, layer);
		return path.empty() ? -1.f : PathLength(path);
	}

	bool NavMesh::IsReachable(const glm::vec3& from, const glm::vec3& to, uint32_t layer, float tolerance) const {
		const auto start = Locate(from, layer, defaultProjectDistance);
		if (!start) {
			return false;
		}
		return Locate(to, layer, tolerance, start->island).has_value();
	}

	NavMesh::LineWalk NavMesh::WalkLine(const glm::vec3& start, const glm::vec3& end, uint32_t layerIndex,
		float maxProject) const {
		LineWalk result { start, false, noPolygon };
		const auto from = Locate(start, layerIndex, maxProject);
		if (!from || !IsFinite(end)) {
			return result;
		}

		const Layer& layer = mLayers[layerIndex];
		result.point = from->position;
		result.polygon = from->polygon;

		const glm::vec2 origin = Flat(from->position);
		const glm::vec2 delta = Flat(end) - origin;
		if (glm::dot(delta, delta) <= 1e-12f) {
			result.reachedEnd = true;
			return result;
		}

		// Through each convex polygon the line is inside until the first edge it crosses outward; that
		// edge's neighbour is the next polygon, or there is none and the line has left the mesh.
		uint32_t current = from->polygon;
		float entered = 0;
		for (size_t step = 0; step <= layer.polygons.size() + 16; ++step) {
			const Polygon& polygon = layer.polygons[current];
			float exit = std::numeric_limits<float>::infinity();
			uint32_t exitEdge = polygon.count;
			for (uint32_t edge = 0; edge < polygon.count; ++edge) {
				const glm::vec2 a = Flat(layer.vertices[polygon.first + edge].position);
				const glm::vec2 b = Flat(layer.vertices[polygon.first + (edge + 1) % polygon.count].position);
				// How far inside the edge the line is: side + t * rate. Going outward is a negative rate.
				const float rate = Cross2(b - a, delta);
				if (rate >= 0) {
					continue;
				}
				const float t = std::max(Cross2(b - a, origin - a) / -rate, entered);
				if (t < exit) {
					exit = t;
					exitEdge = edge;
				}
			}

			if (exitEdge == polygon.count || exit >= 1.f) {
				result.point = { end.x, end.y, HeightAt(polygon, end.x, end.y) };
				result.reachedEnd = true;
				result.polygon = current;
				return result;
			}

			const glm::vec2 at = origin + delta * exit;
			result.point = { at.x, at.y, HeightAt(polygon, at.x, at.y) };
			result.polygon = current;
			const uint32_t neighbour = layer.vertices[polygon.first + exitEdge].neighbour;
			if (neighbour == noPolygon) {
				return result;
			}
			current = neighbour;
			entered = exit;
		}
		return result;
	}
}
