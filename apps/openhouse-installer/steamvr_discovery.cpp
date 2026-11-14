#include "steamvr_discovery.hpp"
#include "steam_metadata.hpp"
#include "process_shutdown.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace openhouse::installer {
namespace {
namespace fs = std::filesystem;
constexpr size_t kMaxLocations = 64;

bool LocalPath(const fs::path& path) {
  const auto root = path.root_name().wstring();
  if (!path.is_absolute() || root.size() != 2 || root[1] != L':') return false;
  const UINT type = GetDriveTypeW(path.root_path().c_str());
  return type == DRIVE_FIXED || type == DRIVE_REMOVABLE;
}

bool Contains(const std::vector<fs::path>& paths, const fs::path& path) {
  return std::any_of(paths.begin(), paths.end(), [&](const auto& previous) {
    return _wcsicmp(previous.c_str(), path.c_str()) == 0;
  });
}

fs::path AddDirectory(std::vector<fs::path>& paths, const fs::path& candidate, bool runtime) {
  if (paths.size() == kMaxLocations || !LocalPath(candidate)) return {};
  std::error_code error;
  const auto directory = fs::canonical(candidate, error);
  if (error || !LocalPath(directory) || !fs::is_directory(directory, error) || error) return {};
  if (runtime && (!fs::is_regular_file(directory / "bin/win64/vrserver.exe", error) || error))
    return {};
  if (!Contains(paths, directory)) paths.push_back(directory);
  return directory;
}

std::string ReadMetadata(const fs::path& path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error) || error) return {};
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const std::streamoff size = input.tellg();
  if (size < 0 || size > std::streamoff(kSteamMetadataLimit)) return {};
  std::string text(size_t(size), '\0');
  input.seekg(0);
  input.read(text.data(), size);
  return input && input.gcount() == size ? text : std::string();
}

fs::path Utf8Path(const std::string& text) {
  try { return fs::path(std::u8string(text.begin(), text.end())); }
  catch (const std::exception&) { return {}; }
}

fs::path EnvironmentPath(const wchar_t* name) {
  const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
  if (!needed || needed > 32768) return {};
  std::wstring value(needed, L'\0');
  const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
  if (!length || length >= needed) return {};
  value.resize(length);
  return fs::path(value);
}
}  // namespace

std::vector<fs::path> DiscoverSteamVr(const std::vector<fs::path>& steamRoots,
                                     const std::vector<fs::path>& runtimeCandidates) {
  std::vector<fs::path> found, libraries;
  for (size_t i = 0; i < std::min(runtimeCandidates.size(), kMaxLocations); ++i)
    AddDirectory(found, runtimeCandidates[i], true);
  for (size_t i = 0; i < std::min(steamRoots.size(), kMaxLocations); ++i) {
    const auto root = AddDirectory(libraries, steamRoots[i], false);
    if (root.empty()) continue;
    for (const auto* metadata : {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"}) {
      for (const auto& text : SteamLibraryPaths(ReadMetadata(root / metadata)))
        AddDirectory(libraries, Utf8Path(text), false);
    }
  }
  for (const auto& library : libraries) {
    const auto apps = library / "steamapps";
    const auto folder = SteamVrInstallFolder(ReadMetadata(apps / "appmanifest_250820.acf"));
    if (!folder.empty()) AddDirectory(found, apps / "common" / Utf8Path(folder), true);
    AddDirectory(found, apps / "common/SteamVR", true);
  }
  return found;
}

std::vector<fs::path> DiscoverInstalledSteamVr() {
  std::vector<fs::path> roots;
  if (const auto registered = shutdown::RegisteredSteamPath(); !registered.empty())
    roots.push_back(registered);
  for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
    if (const auto programFiles = EnvironmentPath(variable); !programFiles.empty())
      roots.push_back(programFiles / "Steam");
  }
  return DiscoverSteamVr(roots, {EnvironmentPath(L"STEAMVR_PATH")});
}
}  // namespace openhouse::installer
