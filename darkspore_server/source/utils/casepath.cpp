
// Include
#include "casepath.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

// utils
namespace utils {
	namespace {
		bool equals_ignoring_case(const std::string& lhs, const std::string& rhs) {
			return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](unsigned char a, unsigned char b) {
				return std::tolower(a) == std::tolower(b);
			});
		}
	}

	std::filesystem::path find_path_ignoring_case(const std::filesystem::path& path) {
		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			return path;
		}

		std::filesystem::path found = path.root_path();
		for (const auto& part : path.relative_path()) {
			const auto exact = found / part;
			if (std::filesystem::exists(exact, error) || part == "." || part == "..") {
				found = exact;
				continue;
			}

			const auto folder = found.empty() ? std::filesystem::path(".") : found;
			const auto name = part.string();
			bool matched = false;
			for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
				if (equals_ignoring_case(entry.path().filename().string(), name)) {
					found /= entry.path().filename();
					matched = true;
					break;
				}
			}
			if (!matched) {
				return {};
			}
		}
		return found;
	}
}
