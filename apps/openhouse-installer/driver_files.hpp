#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "platform/safe_files.hpp"

namespace openhouse::installer {
inline constexpr const char* kPersonal = "openhouse-personal-lighthouse-only";
inline constexpr const char* kUpstream = "liblh session started";

inline bool DriverImage(const std::string& bytes) {
  if (bytes.size() < 64 || bytes.compare(0, 2, "MZ") != 0) return false;
  uint32_t pe = 0;
  std::memcpy(&pe, bytes.data() + 60, sizeof pe);
  if (pe > bytes.size() - 24 || bytes.compare(pe, 4, std::string("PE\0\0", 4)) != 0)
    return false;
  uint16_t machine = 0, flags = 0;
  std::memcpy(&machine, bytes.data() + pe + 4, sizeof machine);
  std::memcpy(&flags, bytes.data() + pe + 22, sizeof flags);
  return machine == 0x8664 && (flags & 0x2000) &&
         bytes.find("HmdDriverFactory") != std::string::npos;
}

inline void RequireStock(const std::string& bytes) {
  if (!DriverImage(bytes) || bytes.find(kPersonal) != std::string::npos ||
      bytes.find(kUpstream) != std::string::npos)
    throw std::runtime_error("Stock driver is missing or unrecognized. Verify SteamVR files in Steam.");
}

inline void Install(const std::filesystem::path& package, const std::filesystem::path& target,
                    const std::filesystem::path& backup) {
  const auto incoming = lh::files::Read(package);
  if (!DriverImage(incoming) || incoming.find(kPersonal) == std::string::npos)
    throw std::runtime_error("Package is not the personal Lighthouse driver");
  const auto current = lh::files::Read(target);
  if (current.find(kPersonal) != std::string::npos) {
    RequireStock(lh::files::Read(backup));
  } else {
    RequireStock(current);
    lh::files::BackupOnce(target, backup);
  }
  lh::files::AtomicWrite(target, incoming);
}

inline void Restore(const std::filesystem::path& target, const std::filesystem::path& backup) {
  const auto stock = lh::files::Read(backup);
  RequireStock(stock);
  const auto current = lh::files::Read(target);
  if (current == stock) return;
  if (!DriverImage(current) || current.find(kPersonal) == std::string::npos)
    throw std::runtime_error("Installed driver is unrecognized. Backup kept; no files changed.");
  lh::files::AtomicWrite(target, stock);
}
}  // namespace openhouse::installer
