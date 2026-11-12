#pragma once
#include <zlib.h>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>
#include "platform/safe_files.hpp"

namespace lh::report {
namespace fs = std::filesystem;

// A plain zip (deflate, data descriptors, no zip64: 4 GB per file and in total).
class ZipWriter {
 public:
  bool Open(const fs::path& path) {
    target_ = path;
    stage_ = std::make_unique<files::StagingDirectory>(path.parent_path());
    out_.open(stage_->Payload(), std::ios::binary | std::ios::trunc);
    return bool(out_);
  }
  bool AddText(const std::string& name, const std::string& text) {
    std::istringstream in(text);
    return Add(name, in);
  }
  bool AddFile(const std::string& name, const fs::path& source) {
    std::ifstream in(source, std::ios::binary);
    return in && Add(name, in);
  }
  bool Close() {
    const uint64_t start = Offset();
    for (const auto& e : entries_) {
      U32(0x02014b50); U16(20); U16(20); U16(0x0808); U16(8); U16(time_); U16(date_);
      U32(e.crc); U32(uint32_t(e.compressed)); U32(uint32_t(e.size));
      U16(uint16_t(e.name.size())); U16(0); U16(0); U16(0); U16(0); U32(0); U32(uint32_t(e.offset));
      out_.write(e.name.data(), std::streamsize(e.name.size()));
    }
    const uint64_t size = Offset() - start;
    if (start > kMax || size > kMax) return false;
    U32(0x06054b50); U16(0); U16(0); U16(uint16_t(entries_.size())); U16(uint16_t(entries_.size()));
    U32(uint32_t(size)); U32(uint32_t(start)); U16(0);
    out_.close();
    if (out_.fail()) return false;
    files::PublishNew(stage_->Payload(), target_);
    return true;
  }

 private:
  static constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
  struct Entry { std::string name; uint32_t crc = 0; uint64_t size = 0, compressed = 0, offset = 0; };

  uint64_t Offset() { return uint64_t(out_.tellp()); }
  void U16(uint16_t v) { const char b[2] = {char(v), char(v >> 8)}; out_.write(b, 2); }
  void U32(uint32_t v) { U16(uint16_t(v)); U16(uint16_t(v >> 16)); }

  bool Add(const std::string& name, std::istream& in) {
    if (!time_) Stamp();
    Entry e{name, 0, 0, 0, Offset()};
    // Flag 0x0808: sizes and CRC follow the data (data descriptor), names are UTF-8.
    U32(0x04034b50); U16(20); U16(0x0808); U16(8); U16(time_); U16(date_);
    U32(0); U32(0); U32(0); U16(uint16_t(name.size())); U16(0);
    out_.write(name.data(), std::streamsize(name.size()));
    z_stream z{};
    if (deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    std::vector<char> input(1 << 16), output(1 << 16);
    uLong crc = crc32(0, nullptr, 0);
    int flush = Z_NO_FLUSH;
    do {
      in.read(input.data(), std::streamsize(input.size()));
      const auto got = size_t(in.gcount());
      crc = crc32(crc, reinterpret_cast<const Bytef*>(input.data()), uInt(got));
      e.size += got;
      flush = in.eof() || got == 0 ? Z_FINISH : Z_NO_FLUSH;
      z.next_in = reinterpret_cast<Bytef*>(input.data());
      z.avail_in = uInt(got);
      do {
        z.next_out = reinterpret_cast<Bytef*>(output.data());
        z.avail_out = uInt(output.size());
        deflate(&z, flush);
        const size_t made = output.size() - z.avail_out;
        out_.write(output.data(), std::streamsize(made));
        e.compressed += made;
      } while (z.avail_out == 0);
    } while (flush != Z_FINISH);
    deflateEnd(&z);
    e.crc = uint32_t(crc);
    if (e.size > kMax || e.compressed > kMax || e.offset > kMax) return false;
    U32(0x08074b50); U32(e.crc); U32(uint32_t(e.compressed)); U32(uint32_t(e.size));
    entries_.push_back(std::move(e));
    return bool(out_);
  }
  void Stamp() {
    const std::time_t t = std::time(nullptr);
    const std::tm local = *std::localtime(&t);
    time_ = uint16_t(local.tm_hour << 11 | local.tm_min << 5 | local.tm_sec / 2);
    date_ = uint16_t((local.tm_year - 80) << 9 | (local.tm_mon + 1) << 5 | local.tm_mday);
  }

  // The stream closes before staging cleanup, including on a failed report.
  std::unique_ptr<files::StagingDirectory> stage_;
  fs::path target_;
  std::ofstream out_;
  std::vector<Entry> entries_;
  uint16_t time_ = 0, date_ = 0;
};
}  // namespace lh::report
