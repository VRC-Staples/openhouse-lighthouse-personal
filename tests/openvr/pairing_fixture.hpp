#pragma once

#include "openvr/pairing_mailbox.hpp"

#include <gtest/gtest.h>
#include <lh/session.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Windows defines a macro with the private mailbox method's name.
#undef SendMessage

namespace lh::openvr::test {
using json = nlohmann::json;

struct FakeMailbox : IVRMailbox {
  std::deque<std::string> inbox;
  std::vector<json> sent;
  std::vector<std::string> addresses;
  std::optional<uint32_t> advertisedLength;
  uint32_t largestRead = 0;
  unsigned registrations = 0, unregistrations = 0, reads = 0;
  bool rejectUnregister = false, rejectRegister = false, omitTerminator = false;
  bool excludeTerminatorFromLength = false;

  int RegisterMailbox(const char*, uint64_t* handle, char* address, uint32_t size) override {
    ++registrations;
    if (rejectRegister) return 1;
    *handle = registrations;
    std::snprintf(address, size, "lighthouse_pairing/test");
    return 0;
  }
  int UndoRegisterMailbox(uint64_t) override {
    ++unregistrations;
    if (rejectUnregister) return 1;
    // The fake host retains an undersized read until its mailbox is unregistered.
    inbox.clear();
    advertisedLength.reset();
    return 0;
  }
  int SendMessage(uint64_t, const char* address, const char* message) override {
    addresses.emplace_back(address);
    sent.push_back(json::parse(message));
    return 0;
  }
  int ReadMessage(uint64_t, char* buffer, uint32_t size, uint32_t* needed) override {
    ++reads;
    largestRead = std::max(largestRead, size);
    *needed = 0;
    if (advertisedLength) { *needed = *advertisedLength; return 1; }
    if (inbox.empty()) return 1;
    const size_t bytes = inbox.front().size() + (omitTerminator ? 0 : 1);
    *needed = uint32_t(inbox.front().size() +
                       (omitTerminator || excludeTerminatorFromLength ? 0 : 1));
    if (bytes > size) return 1;
    std::memcpy(buffer, inbox.front().c_str(), bytes);
    inbox.pop_front();
    return 0;
  }
  std::vector<std::string> Results() const {
    std::vector<std::string> results;
    for (const auto& message : sent)
      if (message.value("type", "") == "pairing_result")
        results.push_back(message.value("result", ""));
    return results;
  }
};

struct FakeContext : vr::IVRDriverContext {
  IVRMailbox* mailbox;
  explicit FakeContext(IVRMailbox* value) : mailbox(value) {}
  void* GetGenericInterface(const char* version, vr::EVRInitError* error) override {
    if (error) *error = vr::VRInitError_None;
    return std::strcmp(version, kMailboxVersion) == 0 ? mailbox : nullptr;
  }
  vr::DriverHandle_t GetDriverHandle() override { return 0; }
};

// No runtime session or hardware is started. Only receiver state and requests are modeled.
struct PairingSession : lh::Session {
  std::vector<Receiver> receivers;
  std::vector<std::pair<std::string, double>> pairing;
  bool acceptPairing = true, acceptStop = true;

  bool Start(std::string*) override { return true; }
  void Stop() override {}
  bool Running() const override { return true; }
  bool Finished() const override { return false; }
  lh::SessionSnapshot Snapshot() const override { return {}; }
  std::vector<lh::DeviceUpdate> Updates() const override { return {}; }
  double HostNow() const override { return 0; }
  std::vector<lh::SessionEvent> PollEvents() override { return {}; }
  void SetDeviceEnabled(const std::string&, bool) override {}
  void SetOutputTuning(double, double, bool) override {}
  void SetMixedLight(const lh::MixedLightOptions&) override {}
  std::vector<Receiver> Receivers() const override { return receivers; }
  bool SetPairing(const std::string& receiver, double seconds) override {
    pairing.emplace_back(receiver, seconds);
    return acceptPairing && (seconds != 0 || acceptStop);
  }
  bool HasBluetoothRadio() const override { return false; }
  bool SetBluetoothRadio(bool) override { return false; }
  std::optional<bool> BluetoothRadioOn() override { return std::nullopt; }
  bool SetCapture(const std::string&) override { return false; }
  bool Haptic(const std::string&, double, double, double) override { return false; }
  bool PowerOff(const std::string&) override { return false; }
  void ResetRoom() override {}
  uint64_t RoomResets() const override { return 0; }
  bool SaveRoom(std::string*) override { return true; }
};

class PairingMailboxTest : public ::testing::Test {
 protected:
  FakeMailbox mailbox;
  FakeContext context{&mailbox};
  PairingSession session;
  PairingMailbox pairing;
  std::vector<std::string> logs;
  inline static std::vector<std::string>* logTarget = nullptr;

  static void Log(const std::string& text) { if (logTarget) logTarget->push_back(text); }
  void SetUp() override {
    logTarget = &logs;
    session.receivers = {{"test-dongle", ""}};
    ASSERT_TRUE(pairing.Open(&context, Log));
  }
  void TearDown() override { logTarget = nullptr; }
  void QueueStart(double seconds = 10, const std::string& receiver = "test-dongle",
                  const std::string& address = "pairing_ui/test") {
    mailbox.inbox.push_back(json{{"type", "start_pairing"}, {"returnAddress", address},
                                {"serial", receiver}, {"timeoutSeconds", seconds}}.dump());
  }
  void Poll() { pairing.Poll(session, ""); }
  void StopPairing() {
    mailbox.inbox.push_back(json{{"type", "stop_pairing"}, {"serial", "test-dongle"},
                                {"returnAddress", "pairing_ui/test"}}.dump());
    Poll();
  }
};

}  // namespace lh::openvr::test
