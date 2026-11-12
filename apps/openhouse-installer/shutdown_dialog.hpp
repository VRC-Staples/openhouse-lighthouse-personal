#pragma once
#include <istream>
#include <ostream>
#include <stdexcept>
#include "process_shutdown.hpp"

namespace openhouse::shutdown {
inline void Show(const std::vector<Process>& processes, std::wostream& output) {
  for (const auto& process : processes)
    output << L"  PID " << process.pid << L": " << process.image.wstring() << L"\n";
}

// No install options are accepted here: --yes cannot authorize either close prompt.
inline void EnsureClosed(const Scope& scope, std::istream& input, std::wostream& output,
                         DWORD gracefulMilliseconds = 15000) {
  auto processes = FindHosts(scope);
  if (processes.empty()) return;
  output << L"These verified processes belong to this Windows user:\n";
  Show(processes, output);
  output << L"Save your work. Request a normal close? [y/N] " << std::flush;
  std::string answer;
  if (!std::getline(input, answer) || (answer != "y" && answer != "Y"))
    throw std::runtime_error("Shutdown canceled. No driver files changed.");
  for (const auto& process : processes) RequestNormalClose(process);
  output << L"Waiting up to " << double(gracefulMilliseconds) / 1000 << L" seconds for normal shutdown.\n"
         << L"Programs without a closeable window can remain open.\n";
  WaitForExit(processes, gracefulMilliseconds);
  processes = FindHosts(scope);
  if (processes.empty()) return;
  output << L"Still running:\n";
  Show(processes, output);
  output << L"Force-close skips cleanup and can lose unsaved VR work.\n"
         << L"Type FORCE to terminate only these processes, or press Enter to cancel: " << std::flush;
  if (!std::getline(input, answer) || !ForceConsent(answer))
    throw std::runtime_error("Force-close canceled. No driver files changed.");
  for (const auto& process : processes) ForceClose(process);
  if (!WaitForExit(processes, 5000) || !FindHosts(scope).empty())
    throw std::runtime_error("A process remained open or restarted. Close it manually and retry.");
}
}  // namespace openhouse::shutdown
