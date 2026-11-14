// Personal Windows installer. Only the driver DLL and its retained backup are changed.
#include <iostream>
#include <string>
#include "installer_actions.hpp"
#include "menu.hpp"
#include "shutdown_dialog.hpp"

namespace {
bool Confirm(const std::wstring& text) {
  std::wcout << text << L" [y/N] " << std::flush;
  std::string answer;
  std::getline(std::cin, answer);
  return answer == "y" || answer == "Y";
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 1) return openhouse::installer::RunMenu();
    const auto options = openhouse::installer::ParseOptions(argc, argv);
    const auto package = options.action == openhouse::installer::Action::Install ?
        openhouse::installer::PackagePath() : std::filesystem::path();
    const openhouse::installer::Prompts prompts{Confirm,
        [](const openhouse::shutdown::Scope& scope) {
          openhouse::shutdown::EnsureClosed(scope, std::cin, std::wcout);
        }};
    std::wcout << openhouse::installer::RunAction(options, package, prompts) << L"\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << "\n";
    return 1;
  }
}
