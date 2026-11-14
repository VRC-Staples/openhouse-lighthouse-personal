#include "process_shutdown.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <stdexcept>
#include "platform/windows_user.hpp"

namespace openhouse::shutdown {
namespace {
namespace fs = std::filesystem;
const wchar_t* kVrNames[] = {L"vrserver.exe", L"vrmonitor.exe", L"vrcompositor.exe",
    L"vrdashboard.exe", L"vrwebhelper.exe", L"vrstartup.exe", L"vrservice.exe"};

bool VrName(const std::wstring& name) {
  for (const auto* candidate : kVrNames) if (_wcsicmp(name.c_str(), candidate) == 0) return true;
  return false;
}

bool Equal(const fs::path& a, const fs::path& b) {
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool Inside(const fs::path& path, const fs::path& root) {
  if (root.empty()) return false;
  auto part = path.begin();
  for (const auto& parent : root) {
    if (part == path.end() || !Equal(*part, parent)) return false;
    ++part;
  }
  return part != path.end();
}

fs::path Image(HANDLE handle) {
  std::wstring buffer(32768, L'\0');
  DWORD length = DWORD(buffer.size());
  if (!QueryFullProcessImageNameW(handle, 0, buffer.data(), &length))
    throw std::runtime_error("Cannot verify a process image. Close it manually.");
  buffer.resize(length);
  return fs::canonical(fs::path(buffer));
}

FILETIME Created(HANDLE handle) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(handle, &created, &exited, &kernel, &user))
    throw std::runtime_error("Cannot verify a process start time. Close it manually.");
  return created;
}

bool SameSession(DWORD pid) {
  DWORD theirs = 0, ours = 0;
  return ProcessIdToSessionId(pid, &theirs) &&
         ProcessIdToSessionId(GetCurrentProcessId(), &ours) && theirs == ours;
}

bool Matches(const Process& process, HANDLE handle) {
  const FILETIME created = Created(handle);
  return CompareFileTime(&created, &process.created) == 0 && SameSession(process.pid) &&
         Equal(Image(handle), process.image) && lh::platform::SameProcessUser(handle);
}

BOOL CALLBACK CloseWindow(HWND window, LPARAM parameter) {
  const auto& process = *reinterpret_cast<const Process*>(parameter);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == process.pid && WaitForSingleObject(process.handle.get(), 0) == WAIT_TIMEOUT)
    PostMessageW(window, WM_CLOSE, 0, 0);
  return TRUE;
}
}  // namespace

bool Process::Running() const {
  const DWORD state = WaitForSingleObject(handle.get(), 0);
  if (state == WAIT_FAILED) throw std::runtime_error("Cannot inspect process state");
  return state == WAIT_TIMEOUT;
}

fs::path RegisteredSteamPath() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS)
    return {};
  wchar_t value[32768]{};
  DWORD size = sizeof value, type = 0;
  const LONG result = RegQueryValueExW(key, L"SteamPath", nullptr, &type,
                                      reinterpret_cast<BYTE*>(value), &size);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t) ||
      size > sizeof value || size % sizeof(wchar_t) || value[size / sizeof(wchar_t) - 1] != L'\0')
    return {};
  return fs::path(value);
}

fs::path RegisteredSteamDirectory() {
  const auto path = RegisteredSteamPath();
  if (path.empty()) return {};
  std::error_code error;
  const auto root = fs::canonical(path, error);
  return error ? fs::path() : root;
}

bool AllowedImage(const fs::path& image, const Scope& scope) {
  const auto name = image.filename().wstring();
  if (VrName(name)) return Inside(image, scope.steamvr);
  if (scope.steam.empty()) return false;
  if (_wcsicmp(name.c_str(), L"steam.exe") == 0) return Equal(image, scope.steam / "steam.exe");
  return _wcsicmp(name.c_str(), L"steamwebhelper.exe") == 0 && Inside(image, scope.steam / "bin");
}

std::vector<Process> FindHosts(const Scope& scope) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snapshot.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate processes");
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof entry;
  std::vector<Process> processes;
  BOOL more = Process32FirstW(snapshot.get(), &entry);
  while (more) {
    const std::wstring name = entry.szExeFile;
    if (VrName(name) || (!scope.steam.empty() && (_wcsicmp(name.c_str(), L"steam.exe") == 0 ||
        _wcsicmp(name.c_str(), L"steamwebhelper.exe") == 0))) {
      Handle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID));
      if (!handle) {
        if (GetLastError() != ERROR_INVALID_PARAMETER)
          throw std::runtime_error("Cannot inspect a Steam/VR process. Close it manually.");
      } else if (WaitForSingleObject(handle.get(), 0) == WAIT_TIMEOUT) {
        const auto image = Image(handle.get());
        if (AllowedImage(image, scope)) {
          if (!lh::platform::SameProcessUser(handle.get()) || !SameSession(entry.th32ProcessID))
            throw std::runtime_error("A target process has another user or session. Close it manually.");
          const FILETIME created = Created(handle.get());
          processes.push_back({entry.th32ProcessID, image, created, std::move(handle)});
        }
      }
    }
    more = Process32NextW(snapshot.get(), &entry);
  }
  if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("Process enumeration failed");
  return processes;
}

void RequestNormalClose(const Process& process) {
  if (!process.Running()) return;
  if (!Matches(process, process.handle.get())) throw std::runtime_error("Process identity changed");
  // WM_CLOSE requests application cleanup. No focus change, new process, or remote code runs.
  if (!EnumWindows(CloseWindow, reinterpret_cast<LPARAM>(&process)))
    throw std::runtime_error("Cannot request a normal window close. Close the application manually.");
}

bool WaitForExit(const std::vector<Process>& processes, DWORD milliseconds) {
  const ULONGLONG end = GetTickCount64() + milliseconds;
  for (;;) {
    const bool any = std::any_of(processes.begin(), processes.end(), [](const auto& p) { return p.Running(); });
    if (!any) return true;
    if (GetTickCount64() >= end) return false;
    Sleep(25);
  }
}

void ForceClose(const Process& process) {
  if (!process.Running()) return;
  Handle target(OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                             FALSE, process.pid));
  if (!target) {
    if (!process.Running()) return;
    throw std::runtime_error("Force-close access denied. No elevation was requested.");
  }
  if (!Matches(process, target.get())) throw std::runtime_error("Process identity changed; force-close refused");
  if (!TerminateProcess(target.get(), 1) && process.Running())
    throw std::runtime_error("Force-close failed. Close the application manually.");
}
}  // namespace openhouse::shutdown
