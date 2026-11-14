#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openhouse::shutdown {
struct CloseHandleOnExit {
  void operator()(HANDLE value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
using Handle = std::unique_ptr<void, CloseHandleOnExit>;

struct Scope {
  std::filesystem::path steamvr;
  std::filesystem::path steam;
};

struct Process {
  DWORD pid = 0;
  std::filesystem::path image;
  FILETIME created{};
  Handle handle;
  bool Running() const;
};

std::filesystem::path RegisteredSteamPath();
std::filesystem::path RegisteredSteamDirectory();
bool AllowedImage(const std::filesystem::path& image, const Scope& scope);
std::vector<Process> FindHosts(const Scope& scope);
void RequestNormalClose(const Process& process);
bool WaitForExit(const std::vector<Process>& processes, DWORD milliseconds);
void ForceClose(const Process& process);

// Kept separate from install confirmation and never controlled by --yes.
inline bool ForceConsent(std::string_view answer) { return answer == "FORCE"; }
}  // namespace openhouse::shutdown
