#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include "common/net.hpp"
#include "openvr/steam_rooms.hpp"
#include "openvr/telemetry_server.hpp"
#include "platform/safe_files.hpp"
#include "telemetry_client.hpp"

namespace {
struct SocketOwner {
  lh::net::Socket value = lh::net::kInvalid;
  ~SocketOwner() { lh::net::Close(value); }
};

bool Receive(lh::net::Socket socket, std::string* out) {
  for (int i = 0; i < 100; ++i) {
    char buffer[4096];
    const long n = lh::net::RecvSome(socket, buffer, sizeof buffer);
    if (n < 0) return false;
    if (n > 0) { out->append(buffer, size_t(n)); return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}
}  // namespace

TEST(PersonalTelemetry, RejectsThirdClientButKeepsFirstViewer) {
  lh::openvr::TelemetryServer server;
  int port = 0;
  for (int candidate = 44400; candidate < 44500 && !port; ++candidate)
    if (server.Start(candidate, "test")) port = candidate;
  ASSERT_NE(port, 0);
  SocketOwner first{lh::net::ConnectLoopback(port)};
  std::string text;
  ASSERT_TRUE(Receive(first.value, &text));
  SocketOwner second{lh::net::ConnectLoopback(port)};
  ASSERT_TRUE(Receive(second.value, &text));
  SocketOwner third{lh::net::ConnectLoopback(port)};
  ASSERT_NE(third.value, lh::net::kInvalid);
  text.clear();
  EXPECT_FALSE(Receive(third.value, &text));
  EXPECT_TRUE(text.empty());
  server.Log(1, lh::LogLevel::Info, "LHR-1", "still working");
  ASSERT_TRUE(Receive(first.value, &text));
  EXPECT_NE(text.find("still working"), std::string::npos);
}

TEST(PersonalViewer, WrongJsonTypeDisconnectsWithoutThrowing) {
  SocketOwner listener;
  int port = 0;
  for (int candidate = 44500; candidate < 44600 && !port; ++candidate) {
    listener.value = lh::net::ListenLoopback(candidate);
    if (listener.value != lh::net::kInvalid) port = candidate;
  }
  ASSERT_NE(port, 0);
  openhouse::viewer::TelemetryClient client(port);
  client.Poll(0);
  SocketOwner peer{lh::net::Accept(listener.value)};
  ASSERT_NE(peer.value, lh::net::kInvalid);
  const std::string messages = "{\"type\":\"hello\"}\n{\"type\":1}\n";
  ASSERT_EQ(lh::net::SendSome(peer.value, messages.data(), messages.size()), messages.size());
  for (int i = 0; i < 20; ++i) {
    EXPECT_NO_THROW(client.Poll(0.01 * i));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_FALSE(client.Connected());
}

TEST(PersonalSteamRooms, UnreadableExistingDatabaseIsNotReplaced) {
#ifndef _WIN32
  GTEST_SKIP() << "Windows file sharing failure injection";
#else
  lh::files::StagingDirectory temp(std::filesystem::temp_directory_path());
  const auto file = temp.Payload();
  const std::string original = R"({"known_objects":[{"serial":"keep-me"}]})";
  lh::files::AtomicWrite(file, original);
  // Deny reads but allow replacement, which exposed the original data-loss path.
  HANDLE lock = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(lock, INVALID_HANDLE_VALUE);
  std::string error;
  EXPECT_FALSE(lh::openvr::WriteUniverse(file.string(), "123", {}, &error));
  CloseHandle(lock);
  EXPECT_EQ(lh::files::Read(file), original);
  EXPECT_FALSE(std::filesystem::exists(file.string() + ".openhouse-backup"));
#endif
}
