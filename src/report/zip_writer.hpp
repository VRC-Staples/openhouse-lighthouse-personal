#pragma once
#include <zlib.h>
#include <algorithm>
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

// Deflate ZIP with data descriptors, no ZIP64. Any failed addition prevents publication.
class ZipWriter {
 public:
  explicit ZipWriter(uint64_t maxBytes = kMax) : limit_(std::min(maxBytes, kMax)) {}
  bool Open(const fs::path& path) {
    if (stage_ || failed_) return Fail();
    target_ = path;
    stage_ = std::make_unique<files::StagingDirectory>(path.parent_path());
    out_.open(stage_->Payload(), std::ios::binary | std::ios::trunc);
    return out_ ? true : Fail();
  }
  bool AddText(const std::string& name, const std::string& text) {
    if (failed_) return false;
    std::istringstream in(text);
    return AddStream(name, in);
  }
  bool AddFile(const std::string& name, const fs::path& source) {
    if (failed_) return false;
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (!ec && size > limit_) return Fail();
    std::ifstream in(source, std::ios::binary);
    return in ? AddStream(name, in) : Fail();
  }
  bool AddStream(const std::string& name, std::istream& in) {
    if (failed_ || !out_.is_open() || name.size() > kMaxShort || entries_.size() >= kMaxShort)
      return Fail();
    if (!time_) Stamp();
    Entry entry{name, 0, 0, 0, written_};
    U32(0x04034b50); U16(20); U16(0x0808); U16(8); U16(time_); U16(date_);
    U32(0); U32(0); U32(0); U16(uint16_t(name.size())); U16(0);
    Write(name.data(), name.size());
    try {
      if (failed_ || !Compress(in, entry)) return Fail();
    } catch (...) {
      Fail();
      throw;
    }
    U32(0x08074b50); U32(entry.crc); U32(uint32_t(entry.compressed)); U32(uint32_t(entry.size));
    if (failed_) return false;
    entries_.push_back(std::move(entry));
    return true;
  }
  bool Close() {
    if (failed_ || !out_.is_open()) return false;
    const uint64_t start = written_;
    for (const auto& e : entries_) {
      U32(0x02014b50); U16(20); U16(20); U16(0x0808); U16(8); U16(time_); U16(date_);
      U32(e.crc); U32(uint32_t(e.compressed)); U32(uint32_t(e.size));
      U16(uint16_t(e.name.size())); U16(0); U16(0); U16(0); U16(0); U32(0); U32(uint32_t(e.offset));
      if (!Write(e.name.data(), e.name.size())) return false;
    }
    const uint64_t size = written_ - start;
    U32(0x06054b50); U16(0); U16(0); U16(uint16_t(entries_.size())); U16(uint16_t(entries_.size()));
    U32(uint32_t(size)); U32(uint32_t(start)); U16(0);
    if (failed_) return false;
    out_.close();
    if (out_.fail()) return Fail();
    files::PublishNew(stage_->Payload(), target_);
    return true;
  }

 private:
  static constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
  static constexpr size_t kMaxShort = std::numeric_limits<uint16_t>::max();
  struct Entry { std::string name; uint32_t crc = 0; uint64_t size = 0, compressed = 0, offset = 0; };
  struct Compressor {
    z_stream stream{};
    bool ready = deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK;
    ~Compressor() { if (ready) deflateEnd(&stream); }
  };

  bool Fail() { failed_ = true; return false; }
  bool Write(const char* bytes, size_t size) {
    if (failed_ || size > limit_ - written_) return Fail();
    out_.write(bytes, std::streamsize(size));
    if (!out_) return Fail();
    written_ += size;
    return true;
  }
  void U16(uint16_t v) { const char b[2] = {char(v), char(v >> 8)}; Write(b, 2); }
  void U32(uint32_t v) { U16(uint16_t(v)); U16(uint16_t(v >> 16)); }

  bool Compress(std::istream& in, Entry& entry) {
    Compressor compressor;
    if (!compressor.ready) return false;
    auto& z = compressor.stream;
    std::vector<char> input(1 << 16), output(1 << 16);
    uLong crc = crc32(0, nullptr, 0);
    int result = Z_OK;
    try {
      while (result != Z_STREAM_END) {
        const auto request = size_t(std::min<uint64_t>(input.size(), limit_ - entry.size + 1));
        in.read(input.data(), std::streamsize(request));
        const auto got = size_t(in.gcount());
        if (in.bad() || (in.fail() && !in.eof()) || got > limit_ - entry.size) return false;
        crc = crc32(crc, reinterpret_cast<const Bytef*>(input.data()), uInt(got));
        entry.size += got;
        const int flush = in.eof() ? Z_FINISH : Z_NO_FLUSH;
        z.next_in = reinterpret_cast<Bytef*>(input.data());
        z.avail_in = uInt(got);
        do {
          z.next_out = reinterpret_cast<Bytef*>(output.data());
          z.avail_out = uInt(output.size());
          result = deflate(&z, flush);
          if (result != Z_OK && result != Z_STREAM_END) return false;
          const size_t made = output.size() - z.avail_out;
          if (!Write(output.data(), made)) return false;
          entry.compressed += made;
        } while (z.avail_in || (flush == Z_FINISH && result != Z_STREAM_END));
      }
    } catch (const std::ios_base::failure&) {
      return false;
    }
    entry.crc = uint32_t(crc);
    return true;
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
  const uint64_t limit_;
  uint64_t written_ = 0;
  uint16_t time_ = 0, date_ = 0;
  bool failed_ = false;
};
}  // namespace lh::report
