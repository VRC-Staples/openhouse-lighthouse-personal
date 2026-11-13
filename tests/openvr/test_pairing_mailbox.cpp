#include "pairing_fixture.hpp"

#include <chrono>
#include <thread>

namespace lh::openvr::test {

TEST_F(PairingMailboxTest, SteamVRsKeepAliveKeepsThePairingGoing) {
  QueueStart();
  Poll();
  QueueStart();
  Poll();
  ASSERT_EQ(session.pairing.size(), 2u);
  EXPECT_EQ(session.pairing[1], std::make_pair(std::string("test-dongle"), 10.0));
  EXPECT_TRUE(mailbox.Results().empty());

  session.receivers[0].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
  ASSERT_FALSE(mailbox.sent.empty());
  EXPECT_EQ(mailbox.sent.back().value("connectedSerial", ""), "LHR-test-new");
}

TEST_F(PairingMailboxTest, AKeepAliveMovesTheDeadline) {
  QueueStart(0.05);
  QueueStart(600);
  Poll();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Poll();
  EXPECT_TRUE(mailbox.Results().empty());

  StopPairing();
  ASSERT_FALSE(session.pairing.empty());
  EXPECT_EQ(session.pairing.back(), std::make_pair(std::string("test-dongle"), 0.0));
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"canceled"});

  QueueStart(0.05);
  Poll();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Poll();
  EXPECT_EQ(mailbox.Results(), (std::vector<std::string>{"canceled", "timeout"}));
}

TEST_F(PairingMailboxTest, AKeepAliveKeepsWhatWasOnTheReceiver) {
  session.receivers[0].connected = "LHR-test-before";
  QueueStart();
  Poll();
  EXPECT_TRUE(mailbox.Results().empty());

  session.receivers[0].connected = "LHR-test-new";
  QueueStart();
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
  ASSERT_FALSE(mailbox.sent.empty());
  EXPECT_EQ(mailbox.sent.back().value("connectedSerial", ""), "LHR-test-new");
}

TEST_F(PairingMailboxTest, AConnectedOriginalDeviceDoesNotFinishPairingWithoutADrop) {
  session.receivers[0].connected = "LHR-test-before";
  QueueStart();
  Poll();
  QueueStart();
  Poll();
  EXPECT_TRUE(mailbox.Results().empty());

  session.receivers[0].connected.clear();
  Poll();
  session.receivers[0].connected = "LHR-test-before";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
}

TEST_F(PairingMailboxTest, SwitchingReceiversCancelsThePreviousPairing) {
  session.receivers.push_back({"test-other-dongle", ""});
  QueueStart();
  Poll();
  QueueStart(10, "test-other-dongle");
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"canceled"});
  EXPECT_EQ(session.pairing, (std::vector<std::pair<std::string, double>>{
      {"test-dongle", 10}, {"test-dongle", 0}, {"test-other-dongle", 10}}));

  session.receivers[0].connected = "LHR-test-old-receiver";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"canceled"});
  session.receivers[1].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(), (std::vector<std::string>{"canceled", "success"}));
}

TEST_F(PairingMailboxTest, FailedStopRejectsSwitchAndKeepsTheOriginalPairing) {
  session.receivers.push_back({"test-other-dongle", ""});
  QueueStart();
  Poll();
  session.acceptStop = false;
  mailbox.inbox.push_back(json{{"type", "start_pairing"}, {"serial", "test-other-dongle"},
                              {"returnAddress", "pairing_ui/other"}, {"message_id", 42}}.dump());
  Poll();
  EXPECT_EQ(session.pairing, (std::vector<std::pair<std::string, double>>{
      {"test-dongle", 10}, {"test-dongle", 0}}));
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"failed_to_start"});
  ASSERT_EQ(mailbox.sent.size(), 1u);
  EXPECT_EQ(mailbox.sent[0].value("message_id", 0), 42);
  EXPECT_EQ(mailbox.addresses[0], "pairing_ui/other");

  session.receivers[0].connected = "LHR-test-original";
  Poll();
  EXPECT_EQ(mailbox.Results(), (std::vector<std::string>{"failed_to_start", "success"}));
  EXPECT_EQ(mailbox.addresses.back(), "pairing_ui/test");
}

TEST_F(PairingMailboxTest, FailedStartLeavesNoActivePairing) {
  session.acceptPairing = false;
  QueueStart();
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"failed_to_start"});
  session.receivers[0].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"failed_to_start"});

  mailbox.inbox.push_back(json{{"type", "discover_dongles"},
                              {"returnAddress", "pairing_ui/test"}}.dump());
  Poll();
  ASSERT_FALSE(mailbox.sent.empty());
  EXPECT_FALSE(mailbox.sent.back().at("isPairing").get<bool>());

  session.receivers[0].connected.clear();
  session.acceptPairing = true;
  QueueStart();
  Poll();
  session.acceptPairing = false;
  QueueStart();
  Poll();
  session.receivers[0].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(),
            (std::vector<std::string>{"failed_to_start", "failed_to_start"}));
}

TEST_F(PairingMailboxTest, StopPreventsALaterSuccess) {
  QueueStart();
  Poll();
  StopPairing();
  session.receivers[0].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"canceled"});
  ASSERT_FALSE(session.pairing.empty());
  EXPECT_EQ(session.pairing.back().second, 0.0);
}

TEST_F(PairingMailboxTest, AKeepAliveWithoutAnAddressPreservesTheReplyDestination) {
  QueueStart();
  Poll();
  QueueStart(10, "test-dongle", "");
  Poll();
  EXPECT_TRUE(mailbox.Results().empty());
  session.receivers[0].connected = "LHR-test-new";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
  ASSERT_FALSE(mailbox.addresses.empty());
  EXPECT_EQ(mailbox.addresses.back(), "pairing_ui/test");
}
}  // namespace
