// Authorize loopback peers by their Windows process token, not their claimed JSON identity.
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <vector>

namespace lh::net {
namespace detail {
inline std::vector<unsigned char> UserSid(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  HANDLE token = nullptr;
  const bool opened = OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE;
  CloseHandle(process);
  if (!opened) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> data(size);
  const bool read = size && GetTokenInformation(token, TokenUser, data.data(), size, &size);
  std::vector<unsigned char> sid;
  if (read) {
    const auto user = reinterpret_cast<const TOKEN_USER*>(data.data());
    sid.resize(GetLengthSid(user->User.Sid));
    if (!CopySid(DWORD(sid.size()), sid.data(), user->User.Sid)) sid.clear();
  }
  CloseHandle(token);
  return sid;
}
}  // namespace detail

// Fail closed if the established peer or its identity cannot be inspected.
inline bool SameUserPeer(SOCKET socket) {
  sockaddr_in local{}, peer{};
  int size = sizeof local;
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &size) != 0) return false;
  size = sizeof peer;
  if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &size) != 0) return false;
  if (peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return false;
  DWORD bytes = 0;
  if (GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) !=
      ERROR_INSUFFICIENT_BUFFER) return false;
  std::vector<unsigned char> data(bytes);
  if (GetExtendedTcpTable(data.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) !=
      NO_ERROR) return false;
  const auto table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(data.data());
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const auto& row = table->table[i];
    if (row.dwState != MIB_TCP_STATE_ESTAB || row.dwLocalAddr != peer.sin_addr.s_addr ||
        row.dwRemoteAddr != local.sin_addr.s_addr || row.dwLocalPort != peer.sin_port ||
        row.dwRemotePort != local.sin_port) continue;
    auto theirs = detail::UserSid(row.dwOwningPid);
    auto ours = detail::UserSid(GetCurrentProcessId());
    return !theirs.empty() && !ours.empty() && EqualSid(theirs.data(), ours.data());
  }
  return false;
}
}  // namespace lh::net
#endif
