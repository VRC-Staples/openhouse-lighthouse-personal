#pragma once
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "log_view.hpp"
#include "views.hpp"
#include "platform/safe_files.hpp"

namespace openhouse::viewer {
struct Settings {
  Detail detail = Detail::Basic;
  static constexpr std::size_t kMaxBytes = 64u << 10;

  bool Load(const std::filesystem::path& path, LogFilter& filter) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error))) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string bytes(kMaxBytes + 1, '\0');
    in.read(bytes.data(), std::streamsize(bytes.size()));
    bytes.resize(std::size_t(in.gcount()));
    if (in.bad() || (in.fail() && !in.eof()) || bytes.size() > kMaxBytes) return false;
    try {
      const auto j = nlohmann::json::parse(bytes);
      if (!j.is_object()) return false;
      const auto nextDetail = j.value("advanced", false) ? Detail::Advanced : Detail::Basic;
      const auto logs = j.value("logs", nlohmann::json::object());
      LogFilter next;
      next.info = logs.value("info", true);
      next.warn = logs.value("warn", true);
      next.error = logs.value("error", true);
      next.debug = logs.value("debug", false);
      next.hideMulti = logs.value("hideMulti", true);
      next.hideLh2 = logs.value("hideLh2", true);
      const auto hidden = logs.value("hidden", nlohmann::json::array());
      if (!hidden.is_array()) return false;
      for (const auto& item : hidden) {
        if (!item.is_string()) return false;
        next.hidden.insert(item.get<std::string>());
      }
      detail = nextDetail;
      filter = std::move(next);
      return true;
    } catch (const nlohmann::json::exception&) {
      return false;
    }
  }

  void Save(const std::filesystem::path& path, const LogFilter& filter) const {
    const nlohmann::json j = {{"advanced", detail == Detail::Advanced},
        {"logs", {{"info", filter.info}, {"warn", filter.warn}, {"error", filter.error},
                  {"debug", filter.debug}, {"hideMulti", filter.hideMulti},
                  {"hideLh2", filter.hideLh2}, {"hidden", filter.hidden}}}};
    const auto bytes = j.dump(2) + "\n";
    if (bytes.size() > kMaxBytes) throw std::runtime_error("Viewer settings exceed the size limit");
    std::filesystem::create_directories(path.parent_path());
    lh::files::AtomicWrite(path, bytes);
  }
};
}  // namespace openhouse::viewer
