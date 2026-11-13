#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <filesystem>
#include "platform/safe_files.hpp"
#include "windows_dll_search.hpp"

int main(int argc, char** argv) {
  assert(argc == 2);
  namespace fs = std::filesystem;
  const fs::path probe = fs::absolute(argv[1]);
  const auto name = probe.filename().wstring();
  HMODULE baseline = LoadLibraryW(name.c_str());
  assert(baseline != nullptr);
  assert(GetProcAddress(baseline, "OpenhouseLoaderProbe") != nullptr);
  assert(FreeLibrary(baseline));
  assert(GetModuleHandleW(name.c_str()) == nullptr);

  assert(openhouse::viewer::RestrictDllSearch());
  assert(LoadLibraryW(name.c_str()) == nullptr);  // Application directory is excluded.
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto copy = temp.Payload().parent_path() / probe.filename();
  fs::copy_file(probe, copy);
  const auto previous = fs::current_path();
  fs::current_path(copy.parent_path());
  assert(SetEnvironmentVariableW(L"PATH", copy.parent_path().c_str()));
  assert(LoadLibraryW(name.c_str()) == nullptr);  // CWD and PATH are excluded too.
  HMODULE explicitPath = LoadLibraryExW(copy.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  assert(explicitPath != nullptr);  // Explicit trusted paths remain supported.
  assert(FreeLibrary(explicitPath));
  fs::current_path(previous);
  fs::remove(copy);

  HMODULE system = LoadLibraryW(L"version.dll");
  assert(system != nullptr);
  wchar_t actual[32768], directory[32768];
  assert(GetModuleFileNameW(system, actual, 32768));
  assert(GetSystemDirectoryW(directory, 32768));
  assert(fs::equivalent(fs::path(actual).parent_path(), fs::path(directory)));
  assert(FreeLibrary(system));
  std::puts("PASS: app/CWD/PATH DLLs refused; system DLLs and explicit paths work");
}
