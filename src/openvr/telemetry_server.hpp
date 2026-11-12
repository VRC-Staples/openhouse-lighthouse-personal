// Serves the driver's live state and log to local viewers (lh/telemetry.hpp).
//
// The driver thread hands it snapshots and log lines. Connection count, queued bytes,
// and nonblocking sends are bounded; slow clients are disconnected.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/net.hpp"
#include "lh/log_level.hpp"
#include "lh/session.hpp"

namespace lh::openvr {

struct RoomStatus {
  bool settled = false;
  unsigned long long epoch = 0;
  double settleSigma = 0;  // m: what a base needs to count as placed
  unsigned long long resets = 0;  // room resets this session
};

class TelemetryServer {
 public:
  TelemetryServer() = default;
  TelemetryServer(const TelemetryServer&) = delete;
  TelemetryServer& operator=(const TelemetryServer&) = delete;
  ~TelemetryServer() { Stop(); }

  // false (and *error) when the port can't be opened; the driver runs on without it.
  bool Start(int port, std::string host, std::string* error = nullptr);
  void Stop();

  void Log(double time, LogLevel level, const std::string& serial, const std::string& text);
  void PublishState(double time, const SessionSnapshot& snapshot, const std::vector<DeviceUpdate>& updates,
                    const std::vector<Session::Receiver>& receivers, const RoomStatus& room);

  static constexpr std::size_t kBacklog = 2000;
  static constexpr std::size_t kMaxQueued = 1 << 20;
  static constexpr std::size_t kMaxClients = 2;
  static constexpr std::size_t kMaxLine = 256 << 10;
  static constexpr std::size_t kSendBudget = 64 << 10;

 private:
  struct Client {
    net::Socket socket = net::kInvalid;
    std::string out;
  };
  void Main();
  void Broadcast(const std::string& line);  // under mutex_

  std::mutex mutex_;
  std::condition_variable wake_;
  std::vector<Client> clients_;
  std::deque<std::string> backlog_;
  std::size_t backlogBytes_ = 0;
  net::Socket listener_ = net::kInvalid;
  std::string host_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

}  // namespace lh::openvr
