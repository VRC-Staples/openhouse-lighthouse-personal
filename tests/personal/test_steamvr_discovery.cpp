// Synthetic metadata and temporary folders only. Never calls DiscoverInstalledSteamVr.
#include <iostream>
#include <stdexcept>
#include "steam_metadata.hpp"
#include "steamvr_discovery.hpp"
#include "platform/safe_files.hpp"

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
namespace {
namespace fs = std::filesystem;
using namespace openhouse::installer;

std::string Quote(const fs::path& path) {
  const auto bytes = path.u8string();
  std::string result = "\"";
  for (const char8_t byte : bytes) {
    const char ch = char(byte);
    if (ch == '\\' || ch == '"') result += '\\';
    result += ch;
  }
  return result + '"';
}

void LibraryMetadata() {
  const auto modern = SteamLibraryPaths(R"vdf(
    // Steam library list
    "libraryfolders" {
      "0" { "path" "C:\\Steam" "apps" { "path" "ignored" "250820" "10" } }
      "1" { "path" "D:\\Steam Library" }
      "label" "not a library"
    })vdf");
  CHECK((modern == std::vector<std::string>{R"(C:\Steam)", R"(D:\Steam Library)"}));
  CHECK((SteamLibraryPaths(R"("LibraryFolders" { "1" "E:\\Steam" })") ==
         std::vector<std::string>{R"(E:\Steam)"}));
  CHECK((SteamLibraryPaths("\xef\xbb\xbf\"libraryfolders\" { 0 { path \"C:\\\\Steam\" } }") ==
         std::vector<std::string>{R"(C:\Steam)"}));
  const auto escaped = SteamLibraryPaths(R"("libraryfolders" { "1" { "path" "D:\\a//b\"c" } })");
  const std::vector<std::string> expectedEscaped = {"D:\\a//b\"c"};
  CHECK(escaped == expectedEscaped);
  const auto unicode = std::string("C:\\Steam ") + "\xc3\xa9";
  CHECK((SteamLibraryPaths("libraryfolders { 0 { path \"" + unicode + "\" } }") ==
         std::vector<std::string>{unicode}));
}

void MalformedMetadata() {
  for (const char* text : {"{", "}", "libraryfolders {", "libraryfolders { 0 }",
                           "libraryfolders { 0 { path \"unterminated }",
                           "libraryfolders { 0 { path \"C:\\\\Steam\" }"})
    CHECK(SteamLibraryPaths(text).empty());
  const std::string prefix = "libraryfolders { 0 { path \"C:\\\\Steam\" ";
  CHECK(SteamLibraryPaths(prefix + "} }" + std::string(kSteamMetadataLimit, ' ')).empty());
  std::string deep = prefix;
  for (int i = 0; i < 15; ++i) deep += "node { ";
  deep += std::string(15, '}') + "} }";
  CHECK(SteamLibraryPaths(deep).empty());
  std::string many = "libraryfolders { ";
  for (int i = 0; i < 65; ++i)
    many += std::to_string(i) + " { path \"C:\\\\Steam\" } ";
  CHECK(SteamLibraryPaths(many + "}").size() == 64);
  const auto embedded = std::string("libraryfolders { 0 { path \"C:\\Steam") +
                        std::string(1, '\0') + "\" } }";
  CHECK(SteamLibraryPaths(embedded).empty());
}

void AppManifest() {
  CHECK(SteamVrInstallFolder(R"("AppState" { "appid" "250820"
        "installdir" "SteamVR Beta" })") == "SteamVR Beta");
  for (const char* folder : {"..", ".", ".. ", "SteamVR.", "../SteamVR", "C:SteamVR",
                             "bad*name", "bad|name"}) {
    CHECK(SteamVrInstallFolder("AppState { appid 250820 installdir \"" +
                              std::string(folder) + "\" }").empty());
  }
  CHECK(SteamVrInstallFolder("AppState { appid 123 installdir SteamVR }").empty());
  CHECK(SteamVrInstallFolder("AppState { installdir SteamVR }").empty());
  CHECK(SteamVrInstallFolder("AppState { appid \"\" appid 250820 installdir SteamVR }").empty());
  CHECK(SteamVrInstallFolder("AppState { appid 250820 installdir \"\" "
                            "installdir SteamVR }").empty());
}

struct Fixture {
  lh::files::StagingDirectory temporary{fs::temp_directory_path()};
  fs::path root = temporary.Payload() / "Steam";
  fs::path other = temporary.Payload() / L"Steam library \u00e9";

  Fixture() {
    fs::create_directories(root / "steamapps");
    fs::create_directories(root / "config");
    fs::create_directories(other / "steamapps");
  }

  fs::path Runtime(const fs::path& library, const char* name = "SteamVR") {
    const auto path = library / "steamapps/common" / name;
    fs::create_directories(path / "bin/win64");
    lh::files::AtomicWrite(path / "bin/win64/vrserver.exe", "fixture only, not executable");
    return fs::canonical(path);
  }

  std::string Libraries() const {
    return "libraryfolders { 0 { path " + Quote(root) + " } 1 { path " + Quote(other) + " } }";
  }
};

void DefaultAndPreferred() {
  Fixture files;
  const auto runtime = files.Runtime(files.root);
  CHECK((DiscoverSteamVr({files.root, files.root / "."}) == std::vector<fs::path>{runtime}));
  CHECK((DiscoverSteamVr({}, {runtime, runtime}) == std::vector<fs::path>{runtime}));
  CHECK(DiscoverSteamVr({{}, "relative/path", L"\\\\not-contacted\\share\\Steam"}).empty());
  CHECK(DiscoverSteamVr({}, {files.other, files.root / "missing"}).empty());
  const auto notExecutable = files.other / "steamapps/common/SteamVR/bin/win64/vrserver.exe";
  fs::create_directories(notExecutable);
  CHECK(DiscoverSteamVr({files.other}).empty());
}

void OtherLibraryAndManifest() {
  Fixture files;
  const auto runtime = files.Runtime(files.other, "SteamVR Beta");
  lh::files::AtomicWrite(files.root / "steamapps/libraryfolders.vdf", files.Libraries());
  lh::files::AtomicWrite(files.other / "steamapps/appmanifest_250820.acf",
                        "AppState { appid 250820 installdir \"SteamVR Beta\" }");
  CHECK((DiscoverSteamVr({files.root}) == std::vector<fs::path>{runtime}));
  const auto second = files.Runtime(files.root);
  const auto found = DiscoverSteamVr({files.root});
  CHECK(found.size() == 2 && found[0] == second && found[1] == runtime);
}

void LegacyAndDamagedFiles() {
  Fixture files;
  const auto runtime = files.Runtime(files.other);
  const auto metadata = files.root / "config/libraryfolders.vdf";
  lh::files::AtomicWrite(metadata, "LibraryFolders { 1 " + Quote(files.other) + " }");
  CHECK((DiscoverSteamVr({files.root}) == std::vector<fs::path>{runtime}));
  lh::files::AtomicWrite(metadata, files.Libraries().substr(0, files.Libraries().size() - 1));
  CHECK(DiscoverSteamVr({files.root}).empty());
  lh::files::AtomicWrite(metadata, files.Libraries() + std::string(kSteamMetadataLimit, ' '));
  CHECK(DiscoverSteamVr({files.root}).empty());
}
}  // namespace

int main() {
  try {
    LibraryMetadata();
    MalformedMetadata();
    AppManifest();
    DefaultAndPreferred();
    OtherLibraryAndManifest();
    LegacyAndDamagedFiles();
    std::cout << "PASS: bounded Steam metadata, library discovery, validation, "
                 "and multiple results\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
