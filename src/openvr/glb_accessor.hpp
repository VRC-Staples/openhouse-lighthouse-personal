#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

namespace lh::openvr {
inline size_t GlbSize(const nlohmann::json& value) {
  if (!value.is_number_integer() ||
      (!value.is_number_unsigned() && value.get<int64_t>() < 0))
    throw std::runtime_error("invalid GLB size");
  const auto n = value.get<uint64_t>();
  if (n > std::numeric_limits<size_t>::max()) throw std::runtime_error("invalid GLB size");
  return size_t(n);
}
inline size_t GlbSize(const nlohmann::json& object, const char* key, size_t fallback) {
  const auto found = object.find(key);
  return found == object.end() ? fallback : GlbSize(*found);
}
inline bool GlbRange(size_t offset, size_t length, size_t total) {
  return offset <= total && length <= total - offset;
}

// Validate the whole accessor against its view before computing a frame address.
inline std::vector<float> GlbValues(const nlohmann::json& document,
                                   const std::vector<uint8_t>& bytes, size_t bin,
                                   size_t binSize, size_t index, size_t frame, size_t count) {
  if ((count != 3 && count != 4) || !GlbRange(bin, binSize, bytes.size()))
    throw std::runtime_error("invalid GLB BIN range");
  const auto& a = document.at("accessors").at(index);
  const auto& v = document.at("bufferViews").at(GlbSize(a.at("bufferView")));
  if (a.at("componentType") != 5126 || a.contains("sparse") ||
      a.at("type") != (count == 3 ? "VEC3" : "VEC4") || GlbSize(v, "buffer", 0) != 0)
    throw std::runtime_error("unsupported accessor");
  const size_t entries = GlbSize(a.at("count")), width = count * sizeof(float);
  const size_t view = GlbSize(v, "byteOffset", 0), length = GlbSize(v.at("byteLength"));
  const size_t offset = GlbSize(a, "byteOffset", 0), stride = GlbSize(v, "byteStride", width);
  if (!entries || stride < width || stride % 4 || view % 4 || offset % 4 ||
      !GlbRange(view, length, binSize) || !GlbRange(offset, width, length) ||
      entries - 1 > (length - offset - width) / stride)
    throw std::runtime_error("bad accessor bounds");
  const size_t selected = std::min(frame, entries - 1);
  const size_t at = bin + view + offset + selected * stride;
  std::vector<float> result(count);
  std::memcpy(result.data(), bytes.data() + at, width);
  for (float value : result)
    if (!std::isfinite(value)) throw std::runtime_error("nonfinite bone");
  return result;
}
}  // namespace lh::openvr
