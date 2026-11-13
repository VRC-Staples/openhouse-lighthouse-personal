#include "log_view.hpp"
#include <cctype>

namespace openhouse::viewer {
std::string LogFilter::Shape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    size_t end = i, lastDigit = std::string::npos;
    while (end < text.size() &&
           (std::isxdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) {
      if (std::isdigit(static_cast<unsigned char>(text[end]))) lastDigit = end;
      ++end;
    }
    if (end == i) { out += text[i++]; continue; }
    const bool wordEnd = end == text.size() ||
                         !std::isalnum(static_cast<unsigned char>(text[end]));
    if (wordEnd && lastDigit != std::string::npos) {
      // A dot inside a word can begin a later eligible suffix, as in "word1.23".
      while (i < end && i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1])))
        out += text[i++];
      if (i <= lastDigit) { out += '#'; i = end; continue; }
    }
    out.append(text, i, end - i);
    i = end;
  }
  return out;
}
}  // namespace openhouse::viewer
