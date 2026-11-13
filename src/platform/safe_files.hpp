// File replacement and device filename boundaries used by the personal fork.
#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace lh::files {
namespace fs = std::filesystem;

inline bool SafeDeviceName(std::string_view name) {
  if (name.empty() || name.size() > 128) return false;
  for (const unsigned char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  std::string upper(name);
  for (char& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return false;
  if (upper.size() == 4 && (upper.substr(0, 3) == "COM" || upper.substr(0, 3) == "LPT") &&
      upper[3] >= '0' && upper[3] <= '9') return false;
  return true;
}

inline std::string Read(const fs::path& path) {
  if (!fs::is_regular_file(fs::symlink_status(path)))
    throw std::runtime_error("Expected a regular file: " + path.string());
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot read: " + path.string());
  std::string bytes;
  char chunk[65536];
  while (in.read(chunk, sizeof chunk)) bytes.append(chunk, sizeof chunk);
  bytes.append(chunk, static_cast<std::size_t>(in.gcount()));
  if (!in.eof() || in.bad()) throw std::runtime_error("Incomplete read: " + path.string());
  return bytes;
}

class StagingDirectory {
 public:
  explicit StagingDirectory(const fs::path& parent) {
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
      path_ = parent / (".openhouse-stage-" + std::to_string(random()) + "-" +
                        std::to_string(random()));
      if (fs::create_directory(path_)) return;
    }
    throw std::runtime_error("Cannot create a private staging directory");
  }
  ~StagingDirectory() {
    std::error_code ec;
    fs::remove(path_ / "payload", ec);
    fs::remove(path_, ec);
  }
  StagingDirectory(const StagingDirectory&) = delete;
  StagingDirectory& operator=(const StagingDirectory&) = delete;
  fs::path Payload() const { return path_ / "payload"; }
 private:
  fs::path path_;
};

// Staging is on the destination volume. A failed replacement leaves the old file intact.
inline void AtomicWrite(const fs::path& target, const std::string& bytes) {
  const auto status = fs::symlink_status(target);
  if (fs::exists(status) && !fs::is_regular_file(status))
    throw std::runtime_error("Refusing to replace a non-regular file: " + target.string());
  StagingDirectory stage(target.parent_path());
  const auto temp = stage.Payload();
  std::ofstream out(temp, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out || Read(temp) != bytes) throw std::runtime_error("Staged write failed");
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("File replacement failed, Windows error " +
                             std::to_string(GetLastError()) + ": " + target.string());
#else
  fs::rename(temp, target);
#endif
}

// Publish a completed staged file without replacing any existing destination.
inline void PublishNew(const fs::path& staged, const fs::path& target) {
#ifdef _WIN32
  if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot publish a new file, Windows error " +
                             std::to_string(GetLastError()) + ": " + target.string());
#else
  // A hard link is atomic and refuses an existing name; staging cleanup removes its link.
  fs::create_hard_link(staged, target);
#endif
}

// Existing backups are never overwritten or removed, including after restoration.
inline void BackupOnce(const fs::path& source, const fs::path& backup) {
  const std::string original = Read(source);
  if (fs::exists(fs::symlink_status(backup))) {
    if (Read(backup) != original) throw std::runtime_error("Existing backup differs: " + backup.string());
    return;
  }
  StagingDirectory stage(backup.parent_path());
  AtomicWrite(stage.Payload(), original);
  try {
    PublishNew(stage.Payload(), backup);
  } catch (const std::exception&) {
    // A concurrent writer is acceptable only if it published the same complete backup.
    if (!fs::exists(fs::symlink_status(backup)) || Read(backup) != original) throw;
  }
}
}  // namespace lh::files
