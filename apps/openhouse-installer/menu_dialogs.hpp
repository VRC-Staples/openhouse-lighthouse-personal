#pragma once
#include <string>
#include "installer_actions.hpp"

namespace openhouse::installer {
inline constexpr UINT kMenuPrompt = WM_APP + 1;

std::wstring ErrorText(const char* message);
std::filesystem::path PickSteamVrFolder(HWND owner);
Prompts MenuPrompts(HWND owner);
}  // namespace openhouse::installer
