#include "menu_dialogs.hpp"
#include <objbase.h>
#include <shobjidl.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "shutdown_dialog.hpp"

namespace openhouse::installer {
namespace {
template <class T> struct ReleaseCom {
  void operator()(T* value) const { if (value) value->Release(); }
};

bool Ask(HWND owner, const std::wstring& text) {
  // Send the prompt to the UI thread while the action worker waits for explicit consent.
  return SendMessageW(owner, kMenuPrompt, 0, reinterpret_cast<LPARAM>(&text)) == IDYES;
}

void CloseNormally(HWND owner, const shutdown::Scope& scope) {
  const auto processes = shutdown::FindHosts(scope);
  if (processes.empty()) return;
  std::wostringstream text;
  text << L"Save your work. Request a normal close for these verified processes?\n\n";
  shutdown::Show(processes, text);
  text << L"\nThese processes belong to this Windows user.\n"
          L"The installer waits up to 15 seconds. It will not force-close applications.";
  if (!Ask(owner, text.str()))
    throw std::runtime_error("Shutdown canceled. No driver files changed.");
  for (const auto& process : processes) shutdown::RequestNormalClose(process);
  shutdown::WaitForExit(processes, 15000);
  if (!shutdown::FindHosts(scope).empty())
    throw std::runtime_error("Some Steam/VR processes remain open or restarted. "
                             "Close them manually and retry. No driver files changed.");
}
}  // namespace

std::wstring ErrorText(const char* message) {
  const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
  if (!length) return L"Cannot read the error message.";
  std::wstring text(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, message, -1, text.data(), length);
  text.resize(size_t(length - 1));
  return text;
}

std::filesystem::path PickSteamVrFolder(HWND owner) {
  IFileOpenDialog* raw = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IFileOpenDialog, reinterpret_cast<void**>(&raw))))
    throw std::runtime_error("Cannot open the folder picker. Enter the SteamVR path directly.");
  std::unique_ptr<IFileOpenDialog, ReleaseCom<IFileOpenDialog>> dialog(raw);
  DWORD flags = 0;
  if (FAILED(dialog->GetOptions(&flags)) ||
      FAILED(dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR)) ||
      FAILED(dialog->SetTitle(L"Choose SteamVR (the folder containing bin\\win64\\vrserver.exe)")))
    throw std::runtime_error("Cannot configure the folder picker. "
                             "Enter the SteamVR path directly.");
  const HRESULT shown = dialog->Show(owner);
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
  if (FAILED(shown)) throw std::runtime_error("Cannot show the SteamVR folder picker.");
  IShellItem* item = nullptr;
  if (FAILED(dialog->GetResult(&item)))
    throw std::runtime_error("Cannot read the selected folder.");
  std::unique_ptr<IShellItem, ReleaseCom<IShellItem>> selected(item);
  PWSTR path = nullptr;
  if (FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    throw std::runtime_error("The selected folder has no Windows file path.");
  std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, CoTaskMemFree);
  return std::filesystem::path(owned.get());
}

Prompts MenuPrompts(HWND owner) {
  return {[owner](const std::wstring& text) { return Ask(owner, text); },
          [owner](const shutdown::Scope& scope) { CloseNormally(owner, scope); }};
}
}  // namespace openhouse::installer
