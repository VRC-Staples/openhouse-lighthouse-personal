#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include "process_shutdown.hpp"

namespace openhouse::installer {
enum class Action { Install, Restore, Status };

struct Options {
  Action action = Action::Status;
  std::filesystem::path steamvr;
  bool yes = false;
};

struct Prompts {
  std::function<bool(const std::wstring&)> confirm;
  std::function<void(const shutdown::Scope&)> close;
};

Options ParseOptions(int argc, const wchar_t* const* argv);
std::filesystem::path PackagePath();
std::wstring RunAction(const Options& options, const std::filesystem::path& package,
                       const Prompts& prompts);
}  // namespace openhouse::installer
