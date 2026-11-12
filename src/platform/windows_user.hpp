// Read a Windows user identity from an already-open process handle.
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <vector>

namespace lh::platform {
inline std::vector<unsigned char> ProcessUserSid(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return {};
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

inline bool SameProcessUser(HANDLE process) {
  auto theirs = ProcessUserSid(process);
  auto ours = ProcessUserSid(GetCurrentProcess());
  return !theirs.empty() && !ours.empty() && EqualSid(theirs.data(), ours.data());
}
}  // namespace lh::platform
#endif
