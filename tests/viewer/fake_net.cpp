// Link only into deterministic client/UI tests, never into an application target.
#include "fake_net.hpp"

#include <algorithm>
#include <cstring>

namespace lh::net {
using openhouse::viewer::test::activeNetwork;

Socket ConnectLoopback(int) {
  ++activeNetwork->blockingCalls;
  return activeNetwork->result == ConnectResult::Connected ? 41 : kInvalid;
}

Socket BeginConnectLoopback(int) {
  ++activeNetwork->begins;
  return activeNetwork->failBegin ? kInvalid : Socket(40 + activeNetwork->begins);
}

ConnectResult PollConnect(Socket) {
  ++activeNetwork->polls;
  return activeNetwork->result;
}

long RecvSome(Socket, char* buffer, std::size_t size) {
  ++activeNetwork->reads;
  if (activeNetwork->failRead) return -1;
  const auto count = std::min(size, activeNetwork->incoming.size());
  std::memcpy(buffer, activeNetwork->incoming.data(), count);
  activeNetwork->incoming.erase(0, count);
  return long(count);
}

void Close(Socket socket) {
  if (socket != kInvalid) activeNetwork->closed.push_back(socket);
}

}  // namespace lh::net
