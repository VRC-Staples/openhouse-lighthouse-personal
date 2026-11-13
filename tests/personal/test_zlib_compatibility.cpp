#ifdef NDEBUG
#undef NDEBUG
#endif
#include <zlib.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::string Payload(size_t count) {
  std::string text(count, '\0');
  unsigned state = 0x51a7e123;
  for (char& byte : text) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    byte = char(state & 0xff);
  }
  return text;
}

std::vector<Bytef> RawDeflate(const std::string& input, size_t capacity) {
  z_stream stream{};
  assert(deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
  std::vector<Bytef> output(capacity);
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = uInt(input.size());
  stream.next_out = output.data();
  stream.avail_out = uInt(output.size());
  const int result = deflate(&stream, Z_FINISH);
  assert(result == Z_STREAM_END);
  assert(stream.avail_in == 0);
  output.resize(stream.total_out);
  assert(deflateEnd(&stream) == Z_OK);
  return output;
}

void CheckCompression(const std::string& input) {
  const auto generous = RawDeflate(input, compressBound(uLong(input.size())) + 64);
  // 1.4 reports Z_STREAM_END even when the final raw-deflate byte fills the buffer.
  assert(RawDeflate(input, generous.size()) == generous);
  std::vector<Bytef> compressed(compressBound(uLong(input.size())));
  uLongf compressedSize = uLongf(compressed.size());
  assert(compress2(compressed.data(), &compressedSize,
                   reinterpret_cast<const Bytef*>(input.data()), uLong(input.size()), 6) == Z_OK);
  std::string decoded(input.size() + 1, '\0');
  uLongf decodedSize = uLongf(decoded.size());
  assert(uncompress(reinterpret_cast<Bytef*>(decoded.data()), &decodedSize,
                    compressed.data(), compressedSize) == Z_OK);
  decoded.resize(decodedSize);
  assert(decoded == input);
}
}  // namespace

int main() {
  assert(std::strcmp(ZLIB_VERSION, "1.4") == 0);
  assert(std::strcmp(zlibVersion(), ZLIB_VERSION) == 0);
  for (size_t count : {size_t(0), size_t(65536), size_t(150000)})
    CheckCompression(Payload(count));
  CheckCompression(R"({"device":"synthetic","imu":{"bias":[0,0,0]}})");
  const std::string check = "123456789";
  assert(crc32(0, reinterpret_cast<const Bytef*>(check.data()), uInt(check.size())) == 0xcbf43926);
  std::puts("zlib 1.4 identity, raw-deflate boundary, uncompress, and CRC checks passed");
}
