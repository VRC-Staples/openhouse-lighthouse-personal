#include "net.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include "peer_user_win.hpp"
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace lh::net {
namespace {
#ifdef _WIN32
bool Started() {
  static const bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}
bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool NonBlocking(Socket s) { u_long on = 1; return ioctlsocket(SOCKET(s), FIONBIO, &on) == 0; }
#else
bool Started() { return true; }
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
bool NonBlocking(Socket s) {
  const int flags = fcntl(int(s), F_GETFL, 0);
  return flags >= 0 && fcntl(int(s), F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

sockaddr_in Loopback(int port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return a;
}

void NoDelay(Socket s) {
  int on = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}
}  // namespace

Socket ListenLoopback(int port, std::string* error) {
  if (!Started()) { if (error) *error = "sockets unavailable"; return kInvalid; }
  const Socket s = Socket(socket(AF_INET, SOCK_STREAM, 0));
  if (s == kInvalid) { if (error) *error = "socket() failed"; return kInvalid; }
  int on = 1;
#ifdef _WIN32
  if (setsockopt(SOCKET(s), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char*>(&on), sizeof on) != 0) {
    Close(s);
    return kInvalid;
  }
#else
  setsockopt(int(s), SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
  const sockaddr_in a = Loopback(port);
  if (bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || listen(s, 4) != 0) {
    if (error) *error = "port " + std::to_string(port) + " is in use";
    Close(s);
    return kInvalid;
  }
  if (!NonBlocking(s)) { Close(s); return kInvalid; }
  return s;
}

Socket Accept(Socket listener) {
  const Socket s = Socket(accept(listener, nullptr, nullptr));
  if (s == kInvalid) return kInvalid;
#ifdef _WIN32
  if (!SameUserPeer(SOCKET(s))) { Close(s); return kInvalid; }
#endif
  if (!NonBlocking(s)) { Close(s); return kInvalid; }
  NoDelay(s);
  return s;
}

Socket ConnectLoopback(int port) {
  if (!Started()) return kInvalid;
  const Socket s = Socket(socket(AF_INET, SOCK_STREAM, 0));
  if (s == kInvalid) return kInvalid;
  const sockaddr_in a = Loopback(port);
  if (connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) { Close(s); return kInvalid; }
#ifdef _WIN32
  if (!SameUserPeer(SOCKET(s))) { Close(s); return kInvalid; }
#endif
  if (!NonBlocking(s)) { Close(s); return kInvalid; }
  NoDelay(s);
  return s;
}

Socket BeginConnectLoopback(int port) {
  if (port < 1 || port > 65535 || !Started()) return kInvalid;
  const Socket s = Socket(socket(AF_INET, SOCK_STREAM, 0));
  if (s == kInvalid) return kInvalid;
  if (!NonBlocking(s)) { Close(s); return kInvalid; }
  const sockaddr_in a = Loopback(port);
  if (connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
#ifdef _WIN32
    const int error = WSAGetLastError();
    const bool pending = error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    const bool pending = errno == EINPROGRESS || WouldBlock();
#endif
    if (!pending) { Close(s); return kInvalid; }
  }
  return s;
}

ConnectResult PollConnect(Socket socket) {
  if (socket == kInvalid) return ConnectResult::Failed;
#ifndef _WIN32
  // POSIX FD_SET indexes by descriptor; reject values outside its fixed storage.
  if (socket < 0 || socket >= FD_SETSIZE) return ConnectResult::Failed;
#endif
  fd_set writable, failed;
  FD_ZERO(&writable);
  FD_ZERO(&failed);
  FD_SET(socket, &writable);
  FD_SET(socket, &failed);
  timeval timeout{};  // Never wait on the caller's UI thread.
#ifdef _WIN32
  const int ready = select(0, nullptr, &writable, &failed, &timeout);
  int length = sizeof(int);
#else
  const int ready = select(int(socket) + 1, nullptr, &writable, &failed, &timeout);
  socklen_t length = sizeof(int);
#endif
  if (ready < 0) return ConnectResult::Failed;
  if (ready == 0) return ConnectResult::Pending;
  int error = 0;
  if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0)
    return ConnectResult::Failed;
#ifdef _WIN32
  if (!SameUserPeer(SOCKET(socket))) return ConnectResult::Failed;
#endif
  NoDelay(socket);
  return ConnectResult::Connected;
}

long SendSome(Socket socket, const char* data, std::size_t size) {
#ifdef _WIN32
  const int n = send(SOCKET(socket), data, int(size), 0);
#else
  const long n = long(send(int(socket), data, size, MSG_NOSIGNAL));
#endif
  if (n >= 0) return long(n);
  return WouldBlock() ? 0 : -1;
}

long RecvSome(Socket socket, char* buffer, std::size_t size) {
#ifdef _WIN32
  const int n = recv(SOCKET(socket), buffer, int(size), 0);
#else
  const long n = long(recv(int(socket), buffer, size, 0));
#endif
  if (n > 0) return long(n);
  if (n == 0) return -1;  // closed
  return WouldBlock() ? 0 : -1;
}

void Close(Socket socket) {
  if (socket == kInvalid) return;
#ifdef _WIN32
  closesocket(SOCKET(socket));
#else
  close(int(socket));
#endif
}

}  // namespace lh::net
