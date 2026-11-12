// Personal Windows installer. Only the driver DLL and its retained backup are changed.
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include "driver_files.hpp"

namespace {
namespace fs = std::filesystem;
const wchar_t* kHosts[] = {L"steam.exe", L"steamwebhelper.exe", L"vrserver.exe",
    L"vrmonitor.exe", L"vrcompositor.exe", L"vrdashboard.exe", L"vrwebhelper.exe",
    L"vrstartup.exe", L"vrservice.exe", L"monado-service.exe"};

void RequireClosedHosts() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot inspect running processes. Installation stopped.");
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof entry;
  bool found = false;
  BOOL more = Process32FirstW(snapshot, &entry);
  while (more) {
    for (const auto* name : kHosts) if (_wcsicmp(entry.szExeFile, name) == 0) found = true;
    more = Process32NextW(snapshot, &entry);
  }
  const DWORD error = GetLastError();
  CloseHandle(snapshot);
  if (error != ERROR_NO_MORE_FILES)
    throw std::runtime_error("Process enumeration failed. Installation stopped.");
  if (found)
    throw std::runtime_error("Close Steam, SteamVR, and VR applications before continuing.");
}

struct Options {
  std::wstring action;
  fs::path steamvr;
  bool yes = false;
};

Options Parse(int argc, wchar_t** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--install" || arg == L"--uninstall" || arg == L"--status") {
      if (!options.action.empty()) throw std::runtime_error("Choose exactly one action");
      options.action = arg;
    } else if (arg == L"--steamvr" && i + 1 < argc) options.steamvr = argv[++i];
    else if (arg == L"--yes") options.yes = true;
    else throw std::runtime_error("Unknown or incomplete option");
  }
  if (options.action.empty() || options.steamvr.empty())
    throw std::runtime_error("Usage: openhouse-installer --install|--uninstall|--status "
                             "--steamvr \"PATH\" [--yes]");
  options.steamvr = fs::canonical(options.steamvr);
  if (!fs::is_regular_file(options.steamvr / "bin/win64/vrserver.exe"))
    throw std::runtime_error("Selected directory does not contain SteamVR");
  return options;
}

bool Confirm(const Options& options, const fs::path& target) {
  std::wcout << L"Personal Lighthouse tracker/controller fork. No headset support.\n"
             << L"Driver: " << target.wstring() << L"\n"
             << L"Only this DLL changes. Its backup is retained after uninstall.\n";
  if (options.yes) return true;
  std::cout << "Continue? [y/N] " << std::flush;
  std::string answer;
  std::getline(std::cin, answer);
  return answer == "y" || answer == "Y";
}

int Run(int argc, wchar_t** argv) {
  const Options options = Parse(argc, argv);
  const auto target = options.steamvr / "drivers/lighthouse/bin/win64/driver_lighthouse.dll";
  const auto backup = fs::path(target.wstring() + L".openhouse-personal-stock");
  if (options.action == L"--status") {
    const auto current = lh::files::Read(target);
    std::cout << "Personal driver: "
              << (current.find(openhouse::installer::kPersonal) != std::string::npos ? "yes" : "no")
              << "\nRetained backup: " << (fs::exists(backup) ? "yes" : "no") << "\n";
    return 0;
  }
  RequireClosedHosts();
  if (!Confirm(options, target)) return 0;
  RequireClosedHosts();
  if (options.action == L"--install") {
    wchar_t self[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    if (!size || size >= std::size(self)) throw std::runtime_error("Cannot locate installer package");
    openhouse::installer::Install(fs::path(self).parent_path() / "driver_lighthouse.dll", target, backup);
  } else {
    openhouse::installer::Restore(target, backup);
  }
  std::cout << "Done. The backup remains beside the driver. No startup entries or settings changed.\n";
  return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << "\n";
    return 1;
  }
}
