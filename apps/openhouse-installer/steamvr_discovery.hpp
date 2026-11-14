#pragma once
#include <filesystem>
#include <vector>

namespace openhouse::installer {
std::vector<std::filesystem::path> DiscoverSteamVr(
    const std::vector<std::filesystem::path>& steamRoots,
    const std::vector<std::filesystem::path>& runtimeCandidates = {});
std::vector<std::filesystem::path> DiscoverInstalledSteamVr();
}  // namespace openhouse::installer
