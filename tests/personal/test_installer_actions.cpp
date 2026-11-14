// Temporary files only. Never starts the menu or closes Steam/VR processes.
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "installer_actions.hpp"
#include "driver_files.hpp"

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
namespace {
namespace fs = std::filesystem;
using namespace openhouse::installer;

template <class F> void Refuses(F action) {
  bool refused = false;
  try { action(); } catch (const std::exception&) { refused = true; }
  CHECK(refused);
}

std::string Image(const std::string& marker) {
  std::string bytes(256, '\0');
  bytes.replace(0, 2, "MZ");
  const uint32_t pe = 128;
  const uint16_t machine = 0x8664, flags = 0x2000;
  std::memcpy(bytes.data() + 60, &pe, sizeof pe);
  bytes.replace(pe, 4, std::string("PE\0\0", 4));
  std::memcpy(bytes.data() + pe + 4, &machine, sizeof machine);
  std::memcpy(bytes.data() + pe + 22, &flags, sizeof flags);
  return bytes + "HmdDriverFactory " + marker;
}

void Arguments() {
  const wchar_t* install[] = {L"installer", L"--install", L"--steamvr", L"not inspected", L"--yes"};
  const auto options = ParseOptions(5, install);
  CHECK(options.action == Action::Install && options.steamvr == L"not inspected" && options.yes);
  const wchar_t* restore[] = {L"installer", L"--uninstall", L"--steamvr", L"folder with spaces"};
  CHECK(ParseOptions(4, restore).action == Action::Restore);
  const wchar_t* status[] = {L"installer", L"--status", L"--steamvr", L"folder"};
  CHECK(ParseOptions(4, status).action == Action::Status);
  Refuses([&] { ParseOptions(1, status); });
  Refuses([&] { ParseOptions(2, status); });
  const wchar_t* duplicate[] = {L"installer", L"--status", L"--install", L"--steamvr", L"folder"};
  Refuses([&] { ParseOptions(5, duplicate); });
  const wchar_t* unknown[] = {L"installer", L"--unknown"};
  Refuses([&] { ParseOptions(2, unknown); });
  const wchar_t* incomplete[] = {L"installer", L"--status", L"--steamvr"};
  Refuses([&] { ParseOptions(3, incomplete); });
}

struct Fixture {
  lh::files::StagingDirectory temporary{fs::temp_directory_path()};
  fs::path steamvr = temporary.Payload() / L"SteamVR with spaces";
  fs::path target = steamvr / "drivers/lighthouse/bin/win64/driver_lighthouse.dll";
  fs::path backup = fs::path(target.wstring() + L".openhouse-personal-stock");
  fs::path package = temporary.Payload() / "package.dll";
  std::string stock = Image("stock fixture");
  int confirmations = 0, closes = 0;
  std::wstring lastConfirmation;

  Fixture() {
    fs::create_directories(steamvr / "bin/win64");
    fs::create_directories(target.parent_path());
    lh::files::AtomicWrite(steamvr / "bin/win64/vrserver.exe", "fixture only");
    lh::files::AtomicWrite(target, stock);
    lh::files::AtomicWrite(package, Image(kPersonal));
  }

  Prompts Cancel() {
    return {[this](const std::wstring& text) {
              ++confirmations;
              lastConfirmation = text;
              return false;
            },
            [this](const openhouse::shutdown::Scope&) { ++closes; }};
  }
};

void StatusAndCancellation() {
  Fixture files;
  const auto prompts = files.Cancel();
  const auto status = RunAction({Action::Status, files.steamvr, true}, {}, prompts);
  CHECK(status == L"Personal driver: no\nRetained backup: no");
  CHECK(files.confirmations == 0 && files.closes == 0);
  CHECK(RunAction({Action::Install, files.steamvr}, files.package, prompts) ==
        L"Canceled. No driver files changed.");
  CHECK(files.confirmations == 1 && files.closes == 0);
  CHECK(files.lastConfirmation.find(files.target.wstring()) != std::wstring::npos);
  CHECK(files.lastConfirmation.find(L"separate confirmation") != std::wstring::npos);
  CHECK(lh::files::Read(files.target) == files.stock && !fs::exists(files.backup));
  lh::files::AtomicWrite(files.backup, files.stock);
  CHECK(RunAction({Action::Restore, files.steamvr}, {}, prompts) ==
        L"Canceled. No driver files changed.");
  CHECK(files.confirmations == 2 && files.closes == 0);
  CHECK(lh::files::Read(files.target) == files.stock);
  CHECK(lh::files::Read(files.backup) == files.stock);
}

void InvalidInputsBeforeConsent() {
  Fixture files;
  const auto prompts = files.Cancel();
  Refuses([&] { RunAction({Action::Status, {}}, {}, prompts); });
  Refuses([&] { RunAction({Action::Status, files.temporary.Payload()}, {}, prompts); });
  lh::files::AtomicWrite(files.package, "invalid package");
  Refuses([&] { RunAction({Action::Install, files.steamvr}, files.package, prompts); });
  Refuses([&] { RunAction({Action::Install, files.steamvr, true}, files.package, prompts); });
  Refuses([&] { RunAction({Action::Restore, files.steamvr}, {}, prompts); });
  CHECK(files.confirmations == 0 && files.closes == 0);
  CHECK(lh::files::Read(files.target) == files.stock && !fs::exists(files.backup));
}
}  // namespace

int main() {
  try {
    Arguments();
    StatusAndCancellation();
    InvalidInputsBeforeConsent();
    std::cout << "PASS: installer flags, status, cancellation, and preflight before consent\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
