#include "platform.hpp"
#include <nlohmann/json.hpp>

namespace lh::platform {
std::vector<std::string> JsonStringList(const std::string& text, const std::string& key) {
  const auto document = nlohmann::json::parse(text, nullptr, false);
  if (!document.is_object()) return {};
  const auto array = document.find(key);
  if (array == document.end() || !array->is_array()) return {};
  std::vector<std::string> result;
  for (const auto& value : *array) {
    if (!value.is_string()) return {};
    result.push_back(value.get<std::string>());
  }
  return result;
}
}  // namespace lh::platform
