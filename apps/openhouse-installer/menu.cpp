#include "menu.hpp"
#include "menu_dialogs.hpp"
#include "steamvr_discovery.hpp"
#include <objbase.h>
#include <chrono>
#include <future>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openhouse::installer {
namespace {
constexpr wchar_t kClassName[] = L"OpenHouseInstallerMenu";
enum Control {
  Folder = 100, FindButton, Browse, InstallButton, RestoreButton, StatusButton, ExitButton
};
using Locations = std::vector<std::filesystem::path>;
using MenuResult = std::variant<std::wstring, Locations>;

std::wstring WindowText(HWND control) {
  std::wstring text(size_t(GetWindowTextLengthW(control)) + 1, L'\0');
  const int length = GetWindowTextW(control, text.data(), int(text.size()));
  text.resize(size_t(length));
  return text;
}

std::wstring WindowsLines(const std::wstring& text) {
  std::wstring result;
  for (const wchar_t ch : text) {
    if (ch == L'\n' && (result.empty() || result.back() != L'\r')) result += L'\r';
    result += ch;
  }
  return result;
}

class Menu {
 public:
  ~Menu() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
  }

  int Run() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = instance;
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.lpszClassName = kClassName;
    if (!RegisterClassW(&type)) throw std::runtime_error("Cannot register the installer window.");
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, 600, 376};
    if (!AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_CONTROLPARENT))
      throw std::runtime_error("Cannot size the installer window.");
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, L"OpenHouse installer", style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error("Cannot create the installer window.");
    ShowWindow(window_, SW_SHOWNORMAL);
    SetFocus(folder_);
    if (!PostMessageW(window_, WM_COMMAND, FindButton, 0))
      SetWindowTextW(result_, L"Use Find SteamVR to search, or choose Browse to select a folder.");
    MSG message{};
    int read = 0;
    while ((read = GetMessageW(&message, nullptr, 0, 0)) > 0) {
      if (!IsDialogMessageW(window_, &message)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (read == -1) throw std::runtime_error("Cannot read installer window messages.");
    return int(message.wParam);
  }

 private:
  HWND Add(const wchar_t* type, const wchar_t* text, DWORD style, int id,
           int x, int y, int width, int height, DWORD extended = 0) {
    const auto control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, window_, reinterpret_cast<HMENU>(INT_PTR(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (!control) throw std::runtime_error("Cannot create an installer control.");
    const auto font = font_ ? font_ : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
  }

  void CreateControls() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0))
      font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    Add(L"STATIC", L"Lighthouse driver for trackers and controllers. No headset support.",
        0, 0, 16, 16, 568, 24);
    Add(L"STATIC", L"&SteamVR folder (contains bin\\win64\\vrserver.exe):", 0, 0, 16, 50, 568, 22);
    folder_ = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                  Folder, 16, 76, 326, 220);
    SendMessageW(folder_, CB_LIMITTEXT, 32767, 0);
    inputs_.push_back(folder_);
    inputs_.push_back(Add(L"BUTTON", L"&Find SteamVR", WS_TABSTOP, FindButton, 350, 76, 132, 26));
    inputs_.push_back(Add(L"BUTTON", L"&Browse...", WS_TABSTOP, Browse, 490, 76, 94, 26));
    inputs_.push_back(Add(L"BUTTON", L"&Install driver", WS_TABSTOP,
                         InstallButton, 16, 118, 174, 32));
    inputs_.push_back(Add(L"BUTTON", L"&Restore original driver", WS_TABSTOP,
                         RestoreButton, 198, 118, 190, 32));
    inputs_.push_back(Add(L"BUTTON", L"&Check installation", WS_TABSTOP,
                         StatusButton, 396, 118, 188, 32));
    Add(L"STATIC", L"Result:", 0, 0, 16, 168, 568, 22);
    result_ = Add(L"EDIT", L"Choose your SteamVR folder, then choose an action.\r\n"
                  L"No driver files change until you confirm an install or restore.",
                  WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                  0, 16, 194, 568, 118, WS_EX_CLIENTEDGE);
    inputs_.push_back(Add(L"BUTTON", L"E&xit", WS_TABSTOP, ExitButton, 490, 328, 94, 30));
    if (!SetTimer(window_, 1, 100, nullptr))
      throw std::runtime_error("Cannot start installer progress monitoring.");
  }

  void EnableInputs(bool enabled) {
    for (const auto control : inputs_) EnableWindow(control, enabled);
    EnableMenuItem(GetSystemMenu(window_, FALSE), SC_CLOSE,
                   MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    DrawMenuBar(window_);
  }

  void Start(Action action) {
    if (operation_.valid()) return;
    const Options options{action, WindowText(folder_), false};
    const auto package = action == Action::Install ? PackagePath() : std::filesystem::path();
    const auto owner = window_;
    operation_ = std::async(std::launch::async, [options, package, owner]() -> MenuResult {
      try { return RunAction(options, package, MenuPrompts(owner)); }
      catch (const std::exception& error) { return L"Error: " + ErrorText(error.what()); }
    });
    EnableInputs(false);
    SetWindowTextW(result_, L"Working. Review any confirmation prompts.\r\n"
                   L"If you request normal shutdown, the wait can take up to 15 seconds.");
  }

  void Search() {
    if (operation_.valid()) return;
    operation_ = std::async(std::launch::async, []() -> MenuResult {
      try { return DiscoverInstalledSteamVr(); }
      catch (const std::exception& error) {
        return L"Search failed: " + ErrorText(error.what()) +
               L"\nUse Browse or enter the SteamVR path manually.";
      }
    });
    EnableInputs(false);
    SetWindowTextW(result_, L"Looking for SteamVR in Steam's known local folders.\r\n"
                   L"No installation or application shutdown will run.");
  }

  void ApplyLocations(const Locations& paths) {
    const auto previous = WindowText(folder_);
    SendMessageW(folder_, CB_RESETCONTENT, 0, 0);
    for (const auto& path : paths) {
      const auto added = SendMessageW(folder_, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(path.c_str()));
      if (added == CB_ERR || added == CB_ERRSPACE)
        throw std::runtime_error("Cannot list detected folders. Use Browse instead.");
    }
    if (paths.size() == 1) {
      SendMessageW(folder_, CB_SETCURSEL, 0, 0);
      SetWindowTextW(result_, L"SteamVR found. Review the folder, then choose an action.\r\n"
                     L"No driver files change until you confirm an install or restore.");
    } else {
      SetWindowTextW(folder_, previous.c_str());
      const auto message = paths.empty() ?
          std::wstring(L"SteamVR was not found in Steam's known local folders.\r\n"
                       L"Use Browse or enter the SteamVR path manually.") :
          L"Found " + std::to_wstring(paths.size()) + L" SteamVR installations.\r\n"
          L"Choose a folder from the list, or use Browse. No installation was started.";
      SetWindowTextW(result_, message.c_str());
    }
  }

  void Poll() {
    if (!operation_.valid() ||
        operation_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = operation_.get();
    const auto* paths = std::get_if<Locations>(&result);
    if (paths) ApplyLocations(*paths);
    else SetWindowTextW(result_, WindowsLines(std::get<std::wstring>(result)).c_str());
    EnableInputs(true);
    SetFocus(paths ? folder_ : result_);
  }

  void Command(int id) {
    if (operation_.valid()) return;
    if (id == FindButton) Search();
    else if (id == Browse) {
      const auto path = PickSteamVrFolder(window_);
      if (!path.empty()) SetWindowTextW(folder_, path.c_str());
    } else if (id == InstallButton) Start(Action::Install);
    else if (id == RestoreButton) Start(Action::Restore);
    else if (id == StatusButton) Start(Action::Status);
    else if (id == ExitButton || id == IDCANCEL) SendMessageW(window_, WM_CLOSE, 0, 0);
  }

  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_CREATE: CreateControls(); return 0;
      case WM_COMMAND: Command(LOWORD(wparam)); return 0;
      case WM_TIMER: Poll(); return 0;
      case kMenuPrompt:
        return MessageBoxW(window_, reinterpret_cast<const std::wstring*>(lparam)->c_str(),
            L"OpenHouse installer confirmation", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
      case WM_CLOSE:
        if (!operation_.valid()) DestroyWindow(window_);
        return 0;
      case WM_DESTROY: PostQuitMessage(0); return 0;
      default: return DefWindowProcW(window_, message, wparam, lparam);
    }
  }

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* menu = reinterpret_cast<Menu*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      menu = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      menu->window_ = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu));
    }
    if (!menu) return DefWindowProcW(window, message, wparam, lparam);
    try { return menu->Handle(message, wparam, lparam); }
    catch (const std::exception& error) {
      if (menu->result_) {
        SetWindowTextW(menu->result_, (L"Error: " + ErrorText(error.what())).c_str());
        menu->EnableInputs(!menu->operation_.valid());
      } else {
        MessageBoxW(window, ErrorText(error.what()).c_str(), L"OpenHouse installer", MB_ICONERROR);
      }
      return message == WM_CREATE ? -1 : 0;
    }
  }

  HWND window_ = nullptr, folder_ = nullptr, result_ = nullptr;
  HFONT font_ = nullptr;
  std::vector<HWND> inputs_;
  std::future<MenuResult> operation_;
};

void HideOwnedConsole() {
  DWORD process = 0;
  if (GetConsoleProcessList(&process, 1) == 1 && process == GetCurrentProcessId())
    ShowWindow(GetConsoleWindow(), SW_HIDE);
}
}  // namespace

int RunMenu() {
  HideOwnedConsole();
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) {
    MessageBoxW(nullptr, L"Cannot initialize the Windows folder picker.",
                L"OpenHouse installer", MB_ICONERROR);
    return 1;
  }
  int result = 1;
  try { result = Menu().Run(); }
  catch (const std::exception& error) {
    MessageBoxW(nullptr, ErrorText(error.what()).c_str(), L"OpenHouse installer", MB_ICONERROR);
  }
  CoUninitialize();
  return result;
}
}  // namespace openhouse::installer
