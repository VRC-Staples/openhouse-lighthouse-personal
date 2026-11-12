// Personal Windows installer. Only the driver DLL and its retained backup are changed.
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include "driver_files.hpp"
#include "shutdown_dialog.hpp"

namespace {
namespace fs = std::filesystem;
void RequireReplaceable(const fs::path& target) {
  openhouse::shutdown::Handle file(CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Driver is locked or not writable. No driver files changed.");
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

fs::path PackagePath() {
  wchar_t self[32768]{};
  const DWORD size = GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
  if (!size || size >= std::size(self)) throw std::runtime_error("Cannot locate installer package");
  return fs::path(self).parent_path() / "driver_lighthouse.dll";
}

bool Confirm(const Options& options, const fs::path& target) {
  std::wcout << L"Personal Lighthouse tracker/controller fork. No headset support.\n"
             << L"Driver: " << target.wstring() << L"\n"
             << L"Only this DLL changes. Its backup is retained after uninstall.\n"
             << L"Closing running applications requires separate confirmation.\n";
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
  // Reject a missing or invalid source before asking anyone to close applications.
  const auto package = options.action == L"--install" ? PackagePath() : fs::path();
  if (options.action == L"--install") openhouse::installer::ReadPersonalPackage(package);
  else openhouse::installer::RequireStock(lh::files::Read(backup));
  if (!Confirm(options, target)) return 0;
  const openhouse::shutdown::Scope scope{options.steamvr, openhouse::shutdown::RegisteredSteamDirectory()};
  openhouse::shutdown::EnsureClosed(scope, std::cin, std::wcout);
  if (!openhouse::shutdown::FindHosts(scope).empty())
    throw std::runtime_error("A Steam/VR process restarted. Close it manually and retry.");
  RequireReplaceable(target);
  if (options.action == L"--install") {
    openhouse::installer::Install(package, target, backup);
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
