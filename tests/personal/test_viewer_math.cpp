#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cctype>
#include <random>
#include <limits>
#include <utility>
#include "log_view.hpp"
#include "map_camera.hpp"

using namespace openhouse::viewer;

// Historical behavior is safe to compare only on bounded, short inputs.
std::string ReferenceShape(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    size_t j = i;
    bool digit = false;
    while (j < text.size() &&
           (std::isxdigit(static_cast<unsigned char>(text[j])) || text[j] == '.')) {
      digit |= std::isdigit(static_cast<unsigned char>(text[j])) != 0;
      ++j;
    }
    const bool start = i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]));
    const bool end = j == text.size() || !std::isalnum(static_cast<unsigned char>(text[j]));
    if (j > i && digit && start && end) { out += '#'; i = j; }
    else out += text[i++];
  }
  return out;
}

void CheckShapeCompatibility() {
  std::mt19937 random(42);
  const std::string alphabet = "abcDEF019.xz- _";
  for (int trial = 0; trial < 5000; ++trial) {
    std::string input(random() % 32, ' ');
    for (auto& c : input) c = alphabet[random() % alphabet.size()];
    assert(LogFilter::Shape(input) == ReferenceShape(input));
  }
}

int main() {
  CheckShapeCompatibility();
  Camera camera;
  camera.target = {0, 0, 0};
  camera.distance = 4.8;
  camera.origin = {0, 0};
  camera.size = {640, 480};
  ImVec2 point;
  assert(camera.ProjectLabel({0, 0, 0}, {8, -6}, &point));
  assert(point.x == 328 && point.y == 234);
  const V3 extreme{825337.8773559856, 2.456629975974098, -564639.1663864373};
  assert(camera.Project(extreme, &point));
  assert(point.x > 2147483647.0);
  if (camera.ProjectLabel(extreme, {8, -6}, &point)) {
    std::fprintf(stderr, "FAIL: out-of-range map label was accepted: x=%.0f\n", point.x);
    return 1;
  }
  assert(!camera.ProjectLabel({std::numeric_limits<double>::quiet_NaN(), 0, 0}, {}, &point));
  for (const auto& [input, expected] : {
      std::pair{"", ""}, {"face", "face"}, {"abc123", "#"},
      {"serial 1234", "serial #"}, {"word1.23", "word1.#"},
      {"abc123z", "abc123z"}, {".a.123", "#"}, {"g1...", "g1..."},
      {"x1.abc2", "x1.#"}, {"LHR-123ABC", "LHR-#"},
      {"slot 2 swept axis 1 (300 so far)", "slot # swept axis # (# so far)"}}) {
    assert(LogFilter::Shape(input) == expected);
  }
  const std::string longRun(250000, 'A');
  assert(LogFilter::Shape(longRun) == longRun);
  assert(LogFilter::Shape("x" + longRun + "1.z") == "x" + longRun + "1.z");
  std::puts("PASS: map label bounds and log-shape behavior, including long unchanged runs");
}
