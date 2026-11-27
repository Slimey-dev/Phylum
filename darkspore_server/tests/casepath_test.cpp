
// Include
#include "utils/casepath.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

namespace {
	// A folder laid out like the extracted game data, removed again when the test ends.
	class DataFolder {
		public:
			DataFolder() : mRoot(std::filesystem::temp_directory_path() / "recap_casepath_test") {
				std::filesystem::remove_all(mRoot);
				std::filesystem::create_directories(mRoot / "serverdata" / "level");
				std::filesystem::create_directories(mRoot / "serverdata" / "Modifiers");
				std::ofstream(mRoot / "serverdata" / "level" / "zelems_1.Level.xml") << "<level/>";
				std::ofstream(mRoot / "serverdata" / "Modifiers" / "Stun.lua") << "";
			}

			~DataFolder() {
				std::filesystem::remove_all(mRoot);
			}

			const std::filesystem::path& root() const {
				return mRoot;
			}

		private:
			std::filesystem::path mRoot;
	};
}

TEST_CASE("an exact path comes back unchanged") {
	DataFolder data;
	const auto path = data.root() / "serverdata" / "level" / "zelems_1.Level.xml";
	CHECK(utils::find_path_ignoring_case(path) == path);
}

TEST_CASE("a file name in another case finds the file on disk") {
	DataFolder data;
	const auto found = utils::find_path_ignoring_case(data.root() / "serverdata" / "level" / "zelems_1.level.xml");
	CHECK(found == data.root() / "serverdata" / "level" / "zelems_1.Level.xml");
}

TEST_CASE("folder names are matched without regard to case too") {
	DataFolder data;
	const auto found = utils::find_path_ignoring_case(data.root() / "serverdata" / "modifiers" / "STUN.lua");
	CHECK(found == data.root() / "serverdata" / "Modifiers" / "Stun.lua");
}

TEST_CASE("a missing file gives an empty path") {
	DataFolder data;
	CHECK(utils::find_path_ignoring_case(data.root() / "serverdata" / "level" / "zelems_9.Level.xml").empty());
	CHECK(utils::find_path_ignoring_case(data.root() / "nothing" / "zelems_1.Level.xml").empty());
}

TEST_CASE("a relative path is resolved from the working folder") {
	DataFolder data;
	const auto previous = std::filesystem::current_path();
	std::filesystem::current_path(data.root());
	const auto found = utils::find_path_ignoring_case("serverdata/LEVEL/zelems_1.level.xml");
	std::filesystem::current_path(previous);
	CHECK(found == std::filesystem::path("serverdata") / "level" / "zelems_1.Level.xml");
}
