#include "steam_metadata.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <utility>

namespace openhouse::installer {
namespace {
enum class Kind { Text, Open, Close, End, Invalid };
struct Token { Kind kind; std::string text; };

class Tokens {
 public:
  explicit Tokens(std::string_view text) : text_(text) {
    if (text_.substr(0, 3) == "\xef\xbb\xbf") position_ = 3;
  }

  Token Next() {
    for (;;) {
      while (position_ < text_.size() && Space(text_[position_])) ++position_;
      if (text_.substr(position_, 2) != "//") break;
      const auto end = text_.find('\n', position_);
      position_ = end == text_.npos ? text_.size() : end + 1;
    }
    if (position_ == text_.size()) return {Kind::End, {}};
    const char ch = text_[position_++];
    if (ch == '{') return {Kind::Open, {}};
    if (ch == '}') return {Kind::Close, {}};
    if (ch == '"') return Quoted();
    if (ch == '\0') return {Kind::Invalid, {}};
    std::string value(1, ch);
    while (position_ < text_.size()) {
      const char next = text_[position_];
      if (Space(next) || next == '{' || next == '}' ||
          text_.substr(position_, 2) == "//") break;
      if (next == '\0' || next == '"') return {Kind::Invalid, {}};
      value += next;
      ++position_;
    }
    return {Kind::Text, std::move(value)};
  }

 private:
  static bool Space(char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; }

  Token Quoted() {
    std::string value;
    while (position_ < text_.size()) {
      const char ch = text_[position_++];
      if (ch == '"') return {Kind::Text, std::move(value)};
      if (ch == '\0') return {Kind::Invalid, {}};
      if (ch != '\\') { value += ch; continue; }
      if (position_ == text_.size()) return {Kind::Invalid, {}};
      const char escaped = text_[position_++];
      if (escaped == '\0') return {Kind::Invalid, {}};
      if (escaped != '\\' && escaped != '"') value += '\\';
      value += escaped;
    }
    return {Kind::Invalid, {}};
  }

  std::string_view text_;
  size_t position_ = 0;
};

using Visitor = std::function<void(const std::vector<std::string>&,
                                  const std::string&, const std::string&)>;
bool ReadValues(std::string_view text, const Visitor& visit) {
  if (text.size() > kSteamMetadataLimit) return false;
  Tokens tokens(text);
  std::vector<std::string> parents;
  for (;;) {
    const auto key = tokens.Next();
    if (key.kind == Kind::End) return parents.empty();
    if (key.kind == Kind::Close) {
      if (parents.empty()) return false;
      parents.pop_back();
      continue;
    }
    if (key.kind != Kind::Text || key.text.empty()) return false;
    const auto value = tokens.Next();
    if (value.kind == Kind::Open) {
      if (parents.size() == 16) return false;
      parents.push_back(key.text);
    } else if (value.kind == Kind::Text) visit(parents, key.text, value.text);
    else return false;
  }
}

bool Equal(const std::string& text, const char* expected) {
  const std::string_view other(expected);
  return text.size() == other.size() &&
      std::equal(text.begin(), text.end(), other.begin(), [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
      });
}

bool Index(const std::string& key) {
  return !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char ch) {
    return ch >= '0' && ch <= '9';
  });
}
}  // namespace

std::vector<std::string> SteamLibraryPaths(std::string_view text) {
  std::vector<std::string> paths;
  const bool valid = ReadValues(text, [&](const auto& parents, const auto& key, const auto& value) {
    if (parents.empty() || !Equal(parents[0], "libraryfolders")) return;
    const bool legacy = parents.size() == 1 && Index(key);
    const bool modern = parents.size() == 2 && Index(parents[1]) && Equal(key, "path");
    if ((legacy || modern) && !value.empty() && value.size() <= 32767 && paths.size() < 64)
      paths.push_back(value);
  });
  return valid ? paths : std::vector<std::string>();
}

std::string SteamVrInstallFolder(std::string_view text) {
  std::string app, folder;
  bool duplicate = false, haveApp = false, haveFolder = false;
  const bool valid = ReadValues(text, [&](const auto& parents, const auto& key, const auto& value) {
    if (parents.size() != 1 || !Equal(parents[0], "AppState")) return;
    if (Equal(key, "appid")) { duplicate |= haveApp; haveApp = true; app = value; }
    if (Equal(key, "installdir")) { duplicate |= haveFolder; haveFolder = true; folder = value; }
  });
  if (!valid || duplicate || app != "250820" || folder.empty() || folder.size() > 255 ||
      folder.back() == '.' || folder.back() == ' ' ||
      folder.find_first_of("/\\:*?\"<>|") != folder.npos ||
      std::any_of(folder.begin(), folder.end(), [](unsigned char ch) { return ch < 32; }))
    return {};
  return folder;
}
}  // namespace openhouse::installer
