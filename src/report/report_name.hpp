#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace lh::report {
inline std::string ReportFilename(const std::string& timestamp, uint32_t processId) {
  // Per-process sequence distinguishes rapid and concurrent saves within one second.
  static std::atomic<uint64_t> sequence{0};
  return "openhouse-report-" + timestamp + "-" + std::to_string(processId) + "-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".zip";
}
}  // namespace lh::report
