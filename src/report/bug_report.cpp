#include "lh/bug_report.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "lh/version.hpp"
#include "platform/platform.hpp"
#include "zip_writer.hpp"

#ifdef _WIN32
#include <knownfolders.h>
#include <shlobj.h>
#else
#include <sys/utsname.h>
#endif

namespace lh {
namespace {
namespace fs = std::filesystem;

using report::ZipWriter;

std::string SystemLine() {
#ifdef _WIN32
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof v;
  // RtlGetVersion: GetVersionEx lies about Windows 10/11 without a manifest.
  using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
  if (const auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")); fn && fn(&v) == 0)
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + " build " +
           std::to_string(v.dwBuildNumber);
  return "Windows";
#else
  std::string name = "Linux";
  std::ifstream release("/etc/os-release");
  for (std::string line; std::getline(release, line);)
    if (line.rfind("PRETTY_NAME=", 0) == 0) {
      name = line.substr(12);
      name.erase(std::remove(name.begin(), name.end(), '"'), name.end());
    }
  utsname u{};
  if (uname(&u) == 0) name += std::string(", kernel ") + u.release;
  return name;
#endif
}

std::string Timestamp(const char* format) {
  const std::time_t t = std::time(nullptr);
  char text[64];
  std::strftime(text, sizeof text, format, std::localtime(&t));
  return text;
}

// For names in the zip (flagged UTF-8) and messages. path::string() converts to the
// Windows code page and throws for what it can't hold (#34: nothing here may throw).
std::string Utf8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

// The newest *.lhraw in dir (the settings page's recordings), if any.
fs::path NewestRecording(const fs::path& dir) {
  std::error_code ec;
  fs::path best;
  fs::file_time_type bestTime{};
  for (const auto& e : fs::directory_iterator(dir, ec))
    if (e.path().extension() == ".lhraw" && (best.empty() || e.last_write_time(ec) > bestTime)) {
      best = e.path();
      bestTime = e.last_write_time(ec);
    }
  return best;
}

}  // namespace

std::string BugReportDirectory() {
#ifdef _WIN32
  PWSTR path = nullptr;
  std::string documents;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path))) {
    try {
      documents = fs::path(path).string();
    } catch (const std::exception&) {  // not in the code page: the home fallback below
    }
  }
  CoTaskMemFree(path);
  if (documents.empty()) documents = platform::Home() + "/Documents";
#else
  std::string documents;
  // ~/.config/user-dirs.dirs: XDG_DOCUMENTS_DIR="$HOME/Documents" (localized names too).
  const std::string config = platform::Env("XDG_CONFIG_HOME").empty() ? platform::Home() + "/.config"
                                                                       : platform::Env("XDG_CONFIG_HOME");
  std::ifstream dirs(config + "/user-dirs.dirs");
  for (std::string line; std::getline(dirs, line);)
    if (line.rfind("XDG_DOCUMENTS_DIR=", 0) == 0) {
      documents = line.substr(18);
      documents.erase(std::remove(documents.begin(), documents.end(), '"'), documents.end());
      if (documents.rfind("$HOME", 0) == 0) documents = platform::Home() + documents.substr(5);
    }
  if (documents.empty()) documents = platform::Home() + "/Documents";
#endif
  return documents + "/openhouse reports";
}

namespace {
std::string SaveBugReportOnce(const BugReportOptions& options, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return std::string();
  };
  const fs::path dir = BugReportDirectory();
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return fail("cannot create " + Utf8(dir) + ": " + ec.message());
  // The process id too: the driver and the viewer can both save one in the same second (#46).
  const fs::path zip = dir / ("openhouse-report-" + Timestamp("%Y%m%d-%H%M%S") + "-" +
                              std::to_string(platform::ProcessId()) + ".zip");

  // (name in the zip, source); a missing one is listed as such in info.txt.
  std::vector<std::pair<std::string, fs::path>> files;
  const fs::path config = platform::ConfigDirectory();
  for (const char* name : {"roommap.json", "hostframe.json", "settings.json"})
    files.push_back({std::string("config/") + name, config / name});
  for (const auto& e : fs::directory_iterator(config / "preinit", ec))
    if (e.path().extension() == ".json") files.push_back({"config/preinit/" + Utf8(e.path().filename()), e.path()});
  const fs::path recording = !options.recording.empty() ? fs::path(options.recording)
                                                         : NewestRecording(fs::path(platform::DataDirectory()) / "recordings");
  if (!recording.empty()) files.push_back({"recording/" + Utf8(recording.filename()), recording});
  const fs::path logs = fs::path(platform::SteamRoot()) / "logs";
  for (const char* name : {"vrserver.txt", "vrserver.previous.txt"})
    files.push_back({std::string("steamvr/") + name, logs / name});

  std::ostringstream info;
  info << "openhouse " << kVersion << "\n"
       << "saved " << Timestamp("%Y-%m-%d %H:%M:%S") << "\n"
       << "system: " << SystemLine() << "\n";
  if (recording.empty()) info << "recording: none (turn on Record session in SteamVR's openhouse settings)\n";
  else info << "recording: " << Utf8(recording.filename()) << "\n";
  std::vector<std::pair<std::string, fs::path>> present;
  for (const auto& f : files) {
    // Present if it opens: one that is there but unreadable lost the whole report (#46).
    if (fs::is_regular_file(f.second, ec) && std::ifstream(f.second, std::ios::binary)) present.push_back(f);
    else info << "missing: " << Utf8(f.second) << "\n";
  }
  if (!options.summary.empty()) info << "\n" << options.summary << (options.summary.back() == '\n' ? "" : "\n");

  ZipWriter writer;
  if (!writer.Open(zip)) return fail("cannot write " + Utf8(zip));
  bool ok = writer.AddText("info.txt", info.str());
  if (!options.log.empty()) ok = ok && writer.AddText("openhouse-log.txt", options.log);
  for (const auto& [name, source] : present) ok = ok && writer.AddFile(name, source);
  ok = writer.Close() && ok;
  if (!ok)
    return fail("writing " + Utf8(zip) + " failed (out of space, or a file over 4 GB)");
  return Utf8(zip);
}
}  // namespace

// Nothing may throw out of here: both callers run it on their own thread (#34, #46).
std::string SaveBugReport(const BugReportOptions& options, std::string* error) {
  try {
    return SaveBugReportOnce(options, error);
  } catch (const std::exception& e) {
    if (error) *error = e.what();
  } catch (...) {
    if (error) *error = "unexpected error";
  }
  return {};
}

}  // namespace lh
