// Authorize loopback peers by their Windows process token, not their claimed JSON identity.
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <vector>
#include "platform/windows_user.hpp"

namespace lh::net {

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
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.dwOwningPid);
    if (!process) return false;
    const bool same = lh::platform::SameProcessUser(process);
    CloseHandle(process);
    return same;
  }
  return false;
}
}  // namespace lh::net
#endif
