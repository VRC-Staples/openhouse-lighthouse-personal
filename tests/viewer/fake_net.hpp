#pragma once

#include "telemetry_client.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace openhouse::viewer::test {

struct FakeNetwork {
  net::ConnectResult result = net::ConnectResult::Pending;
  bool failBegin = false, failRead = false;
  unsigned begins = 0, polls = 0, reads = 0, blockingCalls = 0;
  std::vector<net::Socket> closed;
  std::string incoming;
};
inline FakeNetwork* activeNetwork = nullptr;

class FakeClientTest : public testing::Test {
 protected:
  FakeNetwork network;
  std::unique_ptr<TelemetryClient> client;
  double now = 0;

  void SetUp() override {
    activeNetwork = &network;
    client = std::make_unique<TelemetryClient>(12345);
  }
  void TearDown() override {
    client.reset();
    activeNetwork = nullptr;
  }
  void Feed(const std::string& text) {
    network.incoming += text;
    do { client->Poll(now += 0.01); } while (!network.incoming.empty());
  }
  void Hello() {
    network.result = net::ConnectResult::Connected;
    Feed("{\"type\":\"hello\",\"name\":\"openhouse\",\"protocol\":1}\n");
    ASSERT_TRUE(client->Connected());
  }
};

}  // namespace openhouse::viewer::test
