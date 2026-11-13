#include "telemetry_client.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "lh/telemetry.hpp"

namespace openhouse::viewer {
namespace {
using json = nlohmann::json;

const json& List(const json& object, const char* key) {
  static const json empty = json::array();
  const auto it = object.find(key);
  if (it == object.end()) return empty;
  // More than 256 rows per collection is not useful in this personal room viewer.
  if (!it->is_array() || it->size() > 256) throw std::invalid_argument("Invalid telemetry list");
  return *it;
}

int Int(const json& object, const char* key, int fallback = 0) {
  const auto it = object.find(key);
  if (it == object.end()) return fallback;
  if (!it->is_number_integer() || *it < std::numeric_limits<int>::min() ||
      *it > std::numeric_limits<int>::max())
    throw std::invalid_argument("Telemetry integer is outside its range");
  return it->get<int>();
}

template <size_t N>
std::array<double, N> Array(const json& object, const char* key, std::array<double, N> fallback) {
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) return fallback;
  const auto& j = *it;
  if (!j.is_array() || j.size() != N) throw std::invalid_argument("Invalid pose vector");
  for (size_t i = 0; i < N; ++i) {
    const double value = j[i].get<double>();
    // A generous local-room bound prevents overflow in camera and orientation arithmetic.
    const double limit = N == 4 ? 2.0 : 1e6;
    if (!std::isfinite(value) || std::abs(value) > limit)
      throw std::invalid_argument("Pose vector is outside the display range");
    fallback[i] = value;
  }
  return fallback;
}

Base ReadBase(const json& b) {
  Base x;
  x.serial = b.value("serial", "");
  x.channel = b.value("channel", "");
  x.mode = Int(b, "mode");
  x.seen = b.value("seen", false);
  x.placed = b.value("placed", false);
  x.origin = b.value("origin", false);
  x.locked = b.value("locked", false);
  x.observations = Int(b, "observations");
  x.sigma = b.value("sigma", 0.0);
  x.learned = b.value("learned", x.locked || x.sigma <= 0.005);
  x.faults = Int(b, "faults");
  x.p = Array<3>(b, "p", x.p);
  x.q = Array<4>(b, "q", x.q);
  return x;
}

Device ReadDevice(const json& d) {
  Device x;
  x.serial = d.value("serial", "");
  x.role = d.value("role", "");
  x.model = d.value("model", "");
  x.hand = d.value("hand", "");
  x.state = d.value("state", "");
  x.primary = d.value("primary", "");
  x.connected = d.value("connected", false);
  x.enabled = d.value("enabled", true);
  x.charging = d.value("charging", false);
  x.valid = d.value("valid", false);
  if (const auto b = d.find("battery"); b != d.end() && !b->is_null()) x.battery = Int(d, "battery");
  x.p = Array<3>(d, "p", x.p);
  x.v = Array<3>(d, "v", x.v);
  x.q = Array<4>(d, "q", x.q);
  for (const auto& b : List(d, "bases")) x.bases.push_back(b.get<std::string>());
  for (const auto& c : List(d, "coverage"))
    x.coverage.push_back({c.value("base", ""), Int(c, "frames"), c.value("sensors", 0.0)});
  return x;
}

State ReadState(const json& j) {
  State s;
  s.time = j.value("t", 0.0);
  if (const auto r = j.find("room"); r != j.end() && r->is_object()) {
    s.room.id = r->value("id", "");
    s.room.settled = r->value("settled", false);
    s.room.epoch = r->value("epoch", 0ull);
    s.room.settleSigma = r->value("settleSigma", 0.005);
  }
  for (const auto& b : List(j, "bases")) s.bases.push_back(ReadBase(b));
  for (const auto& d : List(j, "devices")) s.devices.push_back(ReadDevice(d));
  for (const auto& r : List(j, "receivers"))
    s.receivers.push_back({r.value("serial", ""), r.value("connected", "")});
  return s;
}
}  // namespace

void TelemetryClient::Disconnect() {
  net::Close(socket_);
  socket_ = net::kInvalid;
  connecting_ = false;
  helloed_ = haveState_ = false;
  buffer_.clear();
  host_.clear();
  version_.clear();
  pid_ = 0;
  state_ = {};
}

bool TelemetryClient::Ready(double now) {
  if (socket_ == net::kInvalid) {
    if (now < nextAttempt_) return false;
    nextAttempt_ = now + 1.0;
    socket_ = net::BeginConnectLoopback(port_);
    if (socket_ == net::kInvalid) return false;
    connecting_ = true;
    connectDeadline_ = now + 2.0;
  }
  if (!connecting_) return true;
  const auto result = now >= connectDeadline_ ? net::ConnectResult::Failed : net::PollConnect(socket_);
  if (result == net::ConnectResult::Pending) return false;
  if (result == net::ConnectResult::Failed) {
    Disconnect();
    nextAttempt_ = now + 1.0;
    return false;
  }
  connecting_ = false;  // TCP completion and same-user authorization both passed.
  return true;
}

void TelemetryClient::Poll(double now) {
  if (!Ready(now)) return;
  char chunk[16 * 1024];
  // Bound incoming work to 64 KiB per UI frame, even when a peer keeps sending.
  for (int i = 0; i < 4; ++i) {
    const long n = net::RecvSome(socket_, chunk, sizeof chunk);
    if (n < 0) { Disconnect(); return; }
    if (n == 0) break;
    buffer_.append(chunk, size_t(n));
  }
  constexpr size_t kMaxLine = 256u << 10;
  size_t start = 0;
  for (size_t nl; (nl = buffer_.find('\n', start)) != std::string::npos; start = nl + 1) {
    if (nl - start > kMaxLine) { Disconnect(); return; }
    try {
      Handle(buffer_.substr(start, nl - start));
    } catch (const json::exception&) {
      Disconnect();
      return;
    } catch (const std::invalid_argument&) {
      Disconnect();
      return;
    }
  }
  buffer_.erase(0, start);
  if (buffer_.size() > kMaxLine) Disconnect();
}

void TelemetryClient::Handle(const std::string& line) {
  const json j = json::parse(line);
  if (!j.is_object()) throw std::invalid_argument("Expected a telemetry object");
  const std::string type = j.value("type", "");
  if (type == "hello") {
    const auto protocol = j.find("protocol");
    if (helloed_ || j.value("name", "") != "openhouse" || protocol == j.end() ||
        !protocol->is_number_integer() || *protocol != lh::telemetry::kProtocolVersion)
      throw std::invalid_argument("Unexpected telemetry greeting");
    host_ = j.value("host", "");
    version_ = j.value("version", "");
    pid_ = Int(j, "pid");
    helloed_ = true;
    return;
  }
  if (!helloed_) throw std::invalid_argument("Telemetry arrived before the greeting");
  if (type == "log") {
    LogLine l;
    l.time = j.value("t", 0.0);
    lh::ParseLogLevel(j.value("level", "INFO"), &l.level);
    l.serial = j.value("serial", "");
    l.text = j.value("text", "");
    logBytes_ += l.text.size() + l.serial.size();
    logs_.push_back(std::move(l));
    ++received_;
    while (logs_.size() > kMaxLogs || logBytes_ > kMaxLogBytes) {
      logBytes_ -= logs_.front().text.size() + logs_.front().serial.size();
      logs_.pop_front();
    }
  } else if (type == "state") {
    state_ = ReadState(j);
    haveState_ = true;
  }
}
}  // namespace openhouse::viewer
