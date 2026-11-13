#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#endif

#include "common/net.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace {
namespace net = lh::net;

struct SocketOwner {
  net::Socket value = net::kInvalid;
  ~SocketOwner() { net::Close(value); }
};

int Port(net::Socket socket) {
  sockaddr_in address{};
#ifdef _WIN32
  int length = sizeof address;
#else
  socklen_t length = sizeof address;
#endif
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  return ntohs(address.sin_port);
}

TEST(ViewerNet, InvalidPortsAndInvalidSocketFailWithoutConnecting) {
  for (const int port : {-1, 0, 65536})
    EXPECT_EQ(net::BeginConnectLoopback(port), net::kInvalid);
  EXPECT_EQ(net::PollConnect(net::kInvalid), net::ConnectResult::Failed);
#ifndef _WIN32
  EXPECT_EQ(net::PollConnect(-2), net::ConnectResult::Failed);
  EXPECT_EQ(net::PollConnect(FD_SETSIZE), net::ConnectResult::Failed);
#endif
}

TEST(ViewerNet, CompletesAuthorizedLoopbackConnectionWithoutBlockingReads) {
  SocketOwner listener{net::ListenLoopback(0)};
  ASSERT_NE(listener.value, net::kInvalid);
  const int port = Port(listener.value);
  ASSERT_GT(port, 0);
  SocketOwner client{net::BeginConnectLoopback(port)}, peer;
  ASSERT_NE(client.value, net::kInvalid);
  auto status = net::ConnectResult::Pending;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (peer.value == net::kInvalid) peer.value = net::Accept(listener.value);
    status = net::PollConnect(client.value);
    if (peer.value != net::kInvalid && status != net::ConnectResult::Pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_NE(peer.value, net::kInvalid);
  ASSERT_EQ(status, net::ConnectResult::Connected);
  char byte;
  EXPECT_EQ(net::RecvSome(client.value, &byte, 1), 0);
  EXPECT_EQ(net::RecvSome(peer.value, &byte, 1), 0);
}

TEST(ViewerNet, RefusedLoopbackConnectionDoesNotBecomeAuthorized) {
  int port = 0;
  {
    SocketOwner listener{net::ListenLoopback(0)};
    ASSERT_NE(listener.value, net::kInvalid);
    port = Port(listener.value);
    ASSERT_GT(port, 0);
  }
  SocketOwner client{net::BeginConnectLoopback(port)};
  if (client.value == net::kInvalid) return;  // Immediate refusal is also a valid failure.
  auto status = net::ConnectResult::Pending;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (status == net::ConnectResult::Pending && std::chrono::steady_clock::now() < deadline) {
    status = net::PollConnect(client.value);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // Winsock may still be pending when the caller's deadline expires. It must not
  // report authorization; SocketOwner closes it here, as the viewer does at its deadline.
  EXPECT_NE(status, net::ConnectResult::Connected);
}
}  // namespace
