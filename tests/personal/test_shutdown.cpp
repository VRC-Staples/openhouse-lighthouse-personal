// Hidden fixture processes only. Never closes the user's actual Steam or VR processes.
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "shutdown_dialog.hpp"
#include "platform/safe_files.hpp"
#include "platform/windows_user.hpp"

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
namespace {
namespace fs = std::filesystem;
using namespace openhouse::shutdown;

template <class F> void Refuses(F action) {
  bool refused = false;
  try { action(); } catch (const std::exception&) { refused = true; }
  CHECK(refused);
}

LRESULT CALLBACK FixtureWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CREATE) {
    const auto create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    SetTimer(window, 1, 30000, nullptr);
    return 0;
  }
  if (message == WM_CLOSE && GetWindowLongPtrW(window, GWLP_USERDATA)) return 0;
  if (message == WM_TIMER) { DestroyWindow(window); return 0; }
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window, message, wparam, lparam);
}

int Fixture(bool ignoreClose, HANDLE ready) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = FixtureWindow;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"OpenhouseShutdownTest";
  if (!RegisterClassW(&wc)) return 2;
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"Hidden shutdown fixture", WS_OVERLAPPED,
      0, 0, 32, 32, nullptr, nullptr, wc.hInstance,
      reinterpret_cast<void*>(static_cast<INT_PTR>(ignoreClose)));
  if (!window) return 3;
  SetEvent(ready);
  CloseHandle(ready);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return 0;
}

class Child {
 public:
  Child(const fs::path& image, bool ignoreClose) {
    job_.reset(CreateJobObjectW(nullptr, nullptr));
    CHECK(job_);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof limits));
    SECURITY_ATTRIBUTES attributes{sizeof attributes, nullptr, TRUE};
    Handle ready(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    CHECK(ready);
    auto command = L"\"" + image.wstring() + L"\" --fixture " +
                   (ignoreClose ? std::wstring(L"ignore ") : std::wstring(L"close ")) +
                   std::to_wstring(reinterpret_cast<ULONG_PTR>(ready.get()));
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    CHECK(CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
    process_.reset(info.hProcess);
    Handle thread(info.hThread);
    if (!AssignProcessToJobObject(job_.get(), process_.get())) {
      TerminateProcess(process_.get(), 2);
      throw std::runtime_error("Cannot contain test child in a job");
    }
    CHECK(ResumeThread(thread.get()) != DWORD(-1));
    CHECK(WaitForSingleObject(ready.get(), 5000) == WAIT_OBJECT_0);
    pid = info.dwProcessId;
  }
  ~Child() {
    job_.reset();
    WaitForSingleObject(process_.get(), 5000);
  }
  bool Running() const { return WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT; }
  DWORD pid = 0;
 private:
  Handle process_, job_;
};

void Boundaries(const Scope& scope) {
  CHECK(AllowedImage(scope.steamvr / "bin/win64/vrserver.exe", scope));
  CHECK(!AllowedImage(scope.steamvr.parent_path() / "SteamVR-other/bin/vrserver.exe", scope));
  CHECK(!AllowedImage(scope.steamvr / "bin/unknown.exe", scope));
  CHECK(!ForceConsent("yes") && !ForceConsent("y") && !ForceConsent("--yes"));
  CHECK(!ForceConsent("") && !ForceConsent("force") && ForceConsent("FORCE"));
  CHECK(!lh::platform::SameProcessUser(nullptr));
}

void NormalClose(const Scope& scope, const fs::path& image) {
  Child child(image, false);
  std::istringstream input("y\n");
  std::wostringstream output;
  EnsureClosed(scope, input, output, 1000);
  CHECK(!child.Running());
  CHECK(output.str().find(L"Type FORCE") == std::wstring::npos);
}

void CancelAndForce(const Scope& scope, const fs::path& image, const fs::path& unrelated) {
  Child outside(unrelated, true);
  Child child(image, true);
  {
    std::istringstream input("\n");
    std::wostringstream output;
    Refuses([&] { EnsureClosed(scope, input, output, 100); });
    CHECK(child.Running() && outside.Running());
  }
  for (const char* answer : {"y\ny\n", "y\n", "y\n--yes\n"}) {
    std::istringstream input(answer);
    std::wostringstream output;
    Refuses([&] { EnsureClosed(scope, input, output, 100); });
    CHECK(child.Running() && outside.Running());
  }
  auto processes = FindHosts(scope);
  CHECK(processes.size() == 1 && processes.front().pid == child.pid);
  processes.front().created.dwLowDateTime ^= 1;
  Refuses([&] { ForceClose(processes.front()); });
  CHECK(child.Running());
  std::istringstream input("y\nFORCE\n");
  std::wostringstream output;
  EnsureClosed(scope, input, output, 100);
  CHECK(!child.Running() && outside.Running());
  CHECK(output.str().find(std::to_wstring(child.pid)) != std::wstring::npos);
  CHECK(output.str().find(unrelated.wstring()) == std::wstring::npos);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--fixture")
    return Fixture(std::string(argv[2]) == "ignore",
                   reinterpret_cast<HANDLE>(std::stoull(argv[3])));
  try {
    lh::files::StagingDirectory temp(fs::temp_directory_path());
    const auto root = temp.Payload();
    const auto vr = root / "SteamVR";
    const auto image = vr / "bin/win64/vrserver.exe";
    const auto unrelated = root / "Other/vrserver.exe";
    fs::create_directories(image.parent_path());
    fs::create_directories(unrelated.parent_path());
    wchar_t self[32768]{};
    CHECK(GetModuleFileNameW(nullptr, self, DWORD(std::size(self))));
    fs::copy_file(self, image);
    fs::copy_file(self, unrelated);
    const Scope scope{fs::canonical(vr), {}};
    Boundaries(scope);
    NormalClose(scope, image);
    CancelAndForce(scope, image, unrelated);
    CHECK(FindHosts(scope).empty());
    fs::remove_all(root);
    std::cout << "PASS: normal close, explicit FORCE, cancellation, path bounds, invalid and stale identity\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
