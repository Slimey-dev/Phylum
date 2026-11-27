
#ifndef _UTILS_CASEPATH_HEADER
#define _UTILS_CASEPATH_HEADER

// Include
#include <filesystem>

// utils
namespace utils {
	// The game asks for files by names whose case can differ from the extracted data
	// ("zelems_1.level.xml" is on disk as "zelems_1.Level.xml"). Windows finds them anyway;
	// this does the same on case-sensitive file systems. Returns the path as it is on disk,
	// matching each folder and file name without regard to ASCII case, or an empty path
	// when nothing matches.
	std::filesystem::path find_path_ignoring_case(const std::filesystem::path& path);
}

#endif
