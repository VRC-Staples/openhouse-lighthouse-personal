#include "pairing_fixture.hpp"

#include <limits>

namespace lh::openvr::test {
namespace {
// Request budget includes the host's terminating NUL. These are protocol expectations,
// not implementation constants: changing the implementation must not loosen the tests.
constexpr uint32_t kMaxMessageBytes = 64 * 1024;
constexpr unsigned kMaxDepth = 64;

std::string Discovery() {
  return json{{"type", "discover_dongles"}, {"returnAddress", "pairing_ui/test"}}.dump();
}

std::string NestedRequest(unsigned arrays) {
  return "{\"type\":\"discover_dongles\",\"returnAddress\":\"pairing_ui/test\",\"ignored\":" +
         std::string(arrays, '[') + "0" + std::string(arrays, ']') + "}";
}
}  // namespace

class PairingInputTest : public PairingMailboxTest {};

TEST_F(PairingInputTest, OversizedLengthIsBoundedAndFreshRequestsResume) {
  mailbox.advertisedLength = kMaxMessageBytes + 1;
  Poll();
  EXPECT_LE(mailbox.largestRead, kMaxMessageBytes);
  EXPECT_EQ(mailbox.unregistrations, 1u);
  EXPECT_EQ(mailbox.registrations, 2u);
  EXPECT_TRUE(mailbox.sent.empty());

  mailbox.inbox.push_back(Discovery());
  Poll();
  ASSERT_EQ(mailbox.sent.size(), 1u);
  EXPECT_EQ(mailbox.sent[0].at("type"), "discover_dongles_response");
}

TEST_F(PairingInputTest, MaximumReportedLengthDoesNotOverflowOrAllocateFromIt) {
  mailbox.advertisedLength = std::numeric_limits<uint32_t>::max();
  Poll();
  EXPECT_LE(mailbox.largestRead, kMaxMessageBytes);
  EXPECT_EQ(mailbox.unregistrations, 1u);
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, UnregisterFailureDisablesReadsInsteadOfSpinning) {
  mailbox.rejectUnregister = true;
  mailbox.advertisedLength = kMaxMessageBytes + 1;
  Poll();
  const unsigned reads = mailbox.reads;
  for (int i = 0; i < 20; ++i) Poll();
  EXPECT_EQ(mailbox.reads, reads);
  EXPECT_EQ(mailbox.unregistrations, 1u);
  EXPECT_EQ(mailbox.registrations, 1u);
}

TEST_F(PairingInputTest, RegisterFailureDisablesReadsInsteadOfUsingAnOldHandle) {
  mailbox.rejectRegister = true;
  mailbox.advertisedLength = kMaxMessageBytes + 1;
  Poll();
  const unsigned reads = mailbox.reads;
  for (int i = 0; i < 20; ++i) Poll();
  EXPECT_EQ(mailbox.reads, reads);
  EXPECT_EQ(mailbox.unregistrations, 1u);
  EXPECT_EQ(mailbox.registrations, 2u);
}

TEST_F(PairingInputTest, MailboxResetPreservesAnAlreadyRunningPairing) {
  QueueStart();
  Poll();
  mailbox.advertisedLength = kMaxMessageBytes + 1;
  Poll();
  EXPECT_EQ(mailbox.registrations, 2u);
  session.receivers[0].connected = "LHR-test-after-reset";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
  EXPECT_EQ(mailbox.addresses.back(), "pairing_ui/test");
  ASSERT_EQ(session.pairing.size(), 1u);
}

TEST_F(PairingInputTest, FailedResetStillTracksTheRunningPairingOutcome) {
  QueueStart();
  Poll();
  mailbox.rejectUnregister = true;
  mailbox.advertisedLength = kMaxMessageBytes + 1;
  Poll();
  logs.clear();
  session.receivers[0].connected = "LHR-test-with-input-disabled";
  Poll();
  EXPECT_TRUE(std::any_of(logs.begin(), logs.end(), [](const std::string& line) {
    return line.find("receiver test-dongle: success") != std::string::npos;
  }));
  EXPECT_TRUE(mailbox.sent.empty());  // No replies through a disabled host interface.
}

TEST_F(PairingInputTest, MessageAtTheByteLimitIsAccepted) {
  std::string message = Discovery();
  message.append(kMaxMessageBytes - 1 - message.size(), ' ');
  mailbox.inbox.push_back(message);
  Poll();
  ASSERT_EQ(mailbox.sent.size(), 1u);
  EXPECT_EQ(mailbox.unregistrations, 0u);
}

TEST_F(PairingInputTest, DeepUnknownFieldsAreRejectedBeforeBuildingTheDom) {
  mailbox.inbox.push_back(NestedRequest(kMaxDepth));
  mailbox.inbox.push_back(Discovery());
  Poll();
  ASSERT_EQ(mailbox.sent.size(), 1u);
  EXPECT_EQ(mailbox.sent[0].at("type"), "discover_dongles_response");
  EXPECT_EQ(mailbox.unregistrations, 0u);
}

TEST_F(PairingInputTest, ThirtyThousandLevelsAreRejectedAndDiscoveryContinues) {
  const std::string message = NestedRequest(30000);
  ASSERT_LT(message.size(), kMaxMessageBytes);
  mailbox.inbox.push_back(message);
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, NestingAtTheLimitIsAccepted) {
  mailbox.inbox.push_back(NestedRequest(kMaxDepth - 1));
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, StructuredMessageIdsAreRejectedAndLaterMessagesContinue) {
  for (const json& id : {json::array({1}), json{{"nested", 1}}}) {
    json message = json::parse(Discovery());
    message["message_id"] = id;
    mailbox.inbox.push_back(message.dump());
  }
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, DeepMessageIdIsRejectedWithoutRecursiveSerialization) {
  mailbox.inbox.push_back("{\"type\":\"discover_dongles\",\"returnAddress\":\"pairing_ui/test\","
                          "\"message_id\":" + std::string(kMaxDepth, '[') + "0" +
                          std::string(kMaxDepth, ']') + "}");
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, RejectedStartCannotCancelAnExistingPairing) {
  QueueStart();
  Poll();
  session.receivers.push_back({"test-other-dongle", ""});
  mailbox.inbox.push_back(json{{"type", "start_pairing"}, {"serial", "test-other-dongle"},
                              {"message_id", json::array({1})}}.dump());
  Poll();
  ASSERT_EQ(session.pairing.size(), 1u);
  EXPECT_TRUE(mailbox.Results().empty());
  session.receivers[0].connected = "LHR-test-still-pairing";
  Poll();
  EXPECT_EQ(mailbox.Results(), std::vector<std::string>{"success"});
}

TEST_F(PairingInputTest, PrimitiveDiscoveryIdsRetainTheirExactJsonTypes) {
  for (const json& id : {json("text-id"), json(17), json(2.5), json(true), json(nullptr)}) {
    json message = json::parse(Discovery());
    message["message_id"] = id;
    mailbox.inbox.push_back(message.dump());
    Poll();
    ASSERT_FALSE(mailbox.sent.empty());
    EXPECT_EQ(mailbox.sent.back().at("message_id"), id);
  }
}

TEST_F(PairingInputTest, QuotedBracketsAndEscapedQuotesDoNotCountAsNesting) {
  json message = json::parse(Discovery());
  message["ignored"] = std::string(100, '[') + "\\\"[[]]\\";
  mailbox.inbox.push_back(message.dump());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, MissingTerminatorIsRejectedWithinTheReportedLength) {
  mailbox.omitTerminator = true;
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_TRUE(mailbox.sent.empty());
  mailbox.omitTerminator = false;
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, HostLengthsExcludingTheTerminatorAreAccepted) {
  mailbox.excludeTerminatorFromLength = true;
  std::string message = Discovery();
  message.append(kMaxMessageBytes - 1 - message.size(), ' ');
  mailbox.inbox.push_back(message);
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, FailedReadAtTheCapCannotBlockTheQueue) {
  mailbox.excludeTerminatorFromLength = true;
  std::string message = Discovery();
  message.append(kMaxMessageBytes - message.size(), ' ');
  mailbox.inbox.push_back(message);
  Poll();
  EXPECT_TRUE(mailbox.sent.empty());
  EXPECT_EQ(mailbox.unregistrations, 1u);
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
}

TEST_F(PairingInputTest, PollKeepsItsSixteenMessageBudget) {
  for (int i = 0; i < 17; ++i) mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 16u);
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 17u);
}

TEST_F(PairingInputTest, MalformedAndWrongTypeRequestsDoNotBlockDiscovery) {
  for (const char* message : {"{", "[1,2]", "null", "{\"type\":false}", "{\"ignored\":[}]"})
    mailbox.inbox.emplace_back(message);
  mailbox.inbox.push_back(Discovery());
  Poll();
  EXPECT_EQ(mailbox.sent.size(), 1u);
  EXPECT_TRUE(session.pairing.empty());
}

}  // namespace lh::openvr::test
