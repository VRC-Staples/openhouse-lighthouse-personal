#include "installer_actions.hpp"
#include <iterator>
#include <stdexcept>
#include "driver_files.hpp"

namespace openhouse::installer {
namespace {
namespace fs = std::filesystem;

void RequireReplaceable(const fs::path& target) {
  shutdown::Handle file(CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Driver is locked or not writable. No driver files changed.");
}

fs::path SteamVrDirectory(const fs::path& directory) {
  if (directory.empty()) throw std::runtime_error("Choose the SteamVR folder first.");
  const auto canonical = fs::canonical(directory);
  if (!fs::is_regular_file(canonical / "bin/win64/vrserver.exe"))
    throw std::runtime_error("Selected directory does not contain SteamVR.");
  return canonical;
}

std::wstring DescribeChange(const fs::path& target) {
  return L"Personal Lighthouse tracker/controller fork. No headset support.\nDriver: " +
         target.wstring() + L"\nOnly this DLL changes. Its backup is retained after restoration.\n"
         L"Closing running applications requires separate confirmation.\nContinue?";
}
}  // namespace

Options ParseOptions(int argc, const wchar_t* const* argv) {
  Options options;
  bool chosen = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--install" || arg == L"--uninstall" || arg == L"--status") {
      if (chosen) throw std::runtime_error("Choose exactly one action");
      chosen = true;
      options.action = arg == L"--install" ? Action::Install :
                       arg == L"--uninstall" ? Action::Restore : Action::Status;
    } else if (arg == L"--steamvr" && i + 1 < argc) options.steamvr = argv[++i];
    else if (arg == L"--yes") options.yes = true;
    else throw std::runtime_error("Unknown or incomplete option");
  }
  if (!chosen || options.steamvr.empty())
    throw std::runtime_error("Usage: openhouse-installer --install|--uninstall|--status "
                             "--steamvr \"PATH\" [--yes]");
  return options;
}

fs::path PackagePath() {
  wchar_t self[32768]{};
  const DWORD size = GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
  if (!size || size >= std::size(self)) throw std::runtime_error("Cannot locate installer package");
  return fs::path(self).parent_path() / "driver_lighthouse.dll";
}

std::wstring RunAction(const Options& options, const fs::path& package, const Prompts& prompts) {
  const auto steamvr = SteamVrDirectory(options.steamvr);
  const auto target = steamvr / "drivers/lighthouse/bin/win64/driver_lighthouse.dll";
  const auto backup = fs::path(target.wstring() + L".openhouse-personal-stock");
  if (options.action == Action::Status) {
    const auto current = lh::files::Read(target);
    return std::wstring(L"Personal driver: ") +
        (current.find(kPersonal) != std::string::npos ? L"yes" : L"no") +
        L"\nRetained backup: " + (fs::exists(backup) ? L"yes" : L"no");
  }
  // Reject an invalid source before asking anyone to close applications.
  if (options.action == Action::Install) ReadPersonalPackage(package);
  else RequireStock(lh::files::Read(backup));
  if (!options.yes && !prompts.confirm(DescribeChange(target)))
    return L"Canceled. No driver files changed.";
  const shutdown::Scope scope{steamvr, shutdown::RegisteredSteamDirectory()};
  prompts.close(scope);
  if (!shutdown::FindHosts(scope).empty())
    throw std::runtime_error("A Steam/VR process restarted. Close it manually and retry.");
  RequireReplaceable(target);
  if (options.action == Action::Install) Install(package, target, backup);
  else Restore(target, backup);
  return L"Done. The backup remains beside the driver. No startup entries or settings changed.";
}
}  // namespace openhouse::installer
