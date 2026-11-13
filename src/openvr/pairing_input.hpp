#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

namespace lh::openvr::pairing_input {

// Scan structure before constructing a DOM or reaching recursive JSON serialization.
// This is only a nesting guard; the JSON parser still validates syntax and UTF-8.
inline bool WithinNestingLimit(std::string_view text) {
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (const char c : text) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') quoted = true;
    else if (c == '[' || c == '{') {
      if (++depth > 64) return false;
    } else if (c == ']' || c == '}') {
      if (depth == 0) return false;
      --depth;
    }
  }
  return !quoted && depth == 0;
}

inline nlohmann::json Parse(std::string_view text) {
  if (!WithinNestingLimit(text)) return nullptr;
  auto message = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
  if (!message.is_object()) return nullptr;
  const auto id = message.find("message_id");
  if (id != message.end() && !id->is_primitive()) return nullptr;
  return message;
}

inline std::string Text(const nlohmann::json& message, const char* key) {
  const auto it = message.find(key);
  return it != message.end() && it->is_string() ? it->get<std::string>() : std::string();
}

}  // namespace lh::openvr::pairing_input
