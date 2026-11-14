#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openhouse::installer {
inline constexpr std::size_t kSteamMetadataLimit = 1u << 20;

std::vector<std::string> SteamLibraryPaths(std::string_view text);
std::string SteamVrInstallFolder(std::string_view text);
}  // namespace openhouse::installer
