#include "fake_net.hpp"

namespace openhouse::viewer::test {
class ViewerConnection : public FakeClientTest {};

TEST_F(ViewerConnection, PollNeverCallsTheSynchronousConnectHelper) {
  client->Poll(0);
  EXPECT_EQ(network.blockingCalls, 0u);
  EXPECT_EQ(network.begins, 1u);
}

TEST_F(ViewerConnection, PendingConnectionIsPolledWithoutReadingOrRestarting) {
  client->Poll(0);
  client->Poll(0.1);
  client->Poll(1.9);
  EXPECT_EQ(network.begins, 1u);
  EXPECT_EQ(network.polls, 3u);
  EXPECT_EQ(network.reads, 0u);
  EXPECT_TRUE(network.closed.empty());
  EXPECT_FALSE(client->Connected());
}

TEST_F(ViewerConnection, DeadlineClosesPendingSocketAndBacksOffBeforeRetry) {
  client->Poll(0);
  client->Poll(2.0);
  ASSERT_EQ(network.closed.size(), 1u);
  EXPECT_EQ(network.closed[0], 41);
  EXPECT_EQ(network.reads, 0u);
  client->Poll(2.9);
  EXPECT_EQ(network.begins, 1u);
  client->Poll(3.0);
  EXPECT_EQ(network.begins, 2u);
}

TEST_F(ViewerConnection, AuthorizationFailureClosesBeforeAnyTelemetryRead) {
  client->Poll(0);
  network.result = net::ConnectResult::Failed;
  client->Poll(0.1);
  EXPECT_EQ(network.closed.size(), 1u);
  EXPECT_EQ(network.reads, 0u);
  client->Poll(1.0);
  EXPECT_EQ(network.begins, 1u);
  client->Poll(1.1);
  EXPECT_EQ(network.begins, 2u);
}

TEST_F(ViewerConnection, AuthorizedConnectionStillRequiresAValidGreeting) {
  client->Poll(0);
  network.result = net::ConnectResult::Connected;
  client->Poll(0.1);
  EXPECT_FALSE(client->Connected());
  Feed("{\"type\":\"hello\",\"name\":\"openhouse\",\"protocol\":1}\n");
  EXPECT_TRUE(client->Connected());
  EXPECT_EQ(network.begins, 1u);
  EXPECT_EQ(network.blockingCalls, 0u);
}

TEST_F(ViewerConnection, FailedBeginRetainsTheOneSecondRetryDelay) {
  network.failBegin = true;
  client->Poll(0);
  client->Poll(0.9);
  EXPECT_EQ(network.begins, 1u);
  client->Poll(1.0);
  EXPECT_EQ(network.begins, 2u);
  EXPECT_TRUE(network.closed.empty());
}

TEST_F(ViewerConnection, DestructionClosesAPendingSocketExactlyOnce) {
  client->Poll(0);
  client.reset();
  EXPECT_EQ(network.closed, std::vector<net::Socket>{41});
}

TEST_F(ViewerConnection, ReadFailureClearsConnectionAndGreetingState) {
  Hello();
  network.failRead = true;
  client->Poll(0.5);
  EXPECT_FALSE(client->Connected());
  EXPECT_FALSE(client->HaveState());
  EXPECT_TRUE(client->Host().empty());
  EXPECT_EQ(network.closed.size(), 1u);
}

}  // namespace openhouse::viewer::test
