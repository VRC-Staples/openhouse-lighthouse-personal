#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include "settings.hpp"
#include "report/zip_writer.hpp"

namespace {
using namespace openhouse::viewer;
using json = nlohmann::json;
namespace fs = std::filesystem;

class ViewerInput : public testing::Test {
 protected:
  void SetUp() override {
    int port = 0;
    for (int candidate = 44800; candidate < 44900 && !port; ++candidate) {
      listener_ = net::ListenLoopback(candidate);
      if (listener_ != net::kInvalid) port = candidate;
    }
    ASSERT_NE(port, 0);
    client = std::make_unique<TelemetryClient>(port);
    client->Poll(0);
    peer_ = net::Accept(listener_);
    ASSERT_NE(peer_, net::kInvalid);
  }
  void TearDown() override { net::Close(peer_); net::Close(listener_); }
  void Send(const std::string& text) {
    std::size_t sent = 0;
    for (int i = 0; i < 1000 && sent < text.size(); ++i) {
      const long count = net::SendSome(peer_, text.data() + sent, text.size() - sent);
      if (count < 0) break;
      sent += std::size_t(count);
      client->Poll(0.1);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < 20; ++i) {
      client->Poll(0.1);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  void Message(const json& message) { Send(message.dump() + "\n"); }
  void Hello() {
    Message({{"type", "hello"}, {"name", "openhouse"}, {"protocol", 1}, {"pid", 123}});
    ASSERT_TRUE(client->Connected());
  }
  void Rejected() {
    EXPECT_FALSE(client->Connected());
    EXPECT_FALSE(client->HaveState());
    char byte;
    EXPECT_LT(net::RecvSome(peer_, &byte, 1), 0);
  }
  std::unique_ptr<TelemetryClient> client;
 private:
  net::Socket listener_ = net::kInvalid, peer_ = net::kInvalid;
};

TEST_F(ViewerInput, KeepsValidStatsAndLogs) {
  Hello();
  Message({{"type", "state"}, {"future_field", 42},
           {"devices", {{{"serial", "LHR-test"}, {"battery", 88}, {"valid", true},
                         {"p", {1.0, 2.0, 3.0}}, {"q", {1.0, 0.0, 0.0, 0.0}}}}}});
  Message({{"type", "log"}, {"text", "tracking works"}});
  ASSERT_TRUE(client->Connected());
  ASSERT_TRUE(client->HaveState());
  ASSERT_EQ(client->Current().devices.size(), 1u);
  EXPECT_EQ(client->Current().devices.front().battery, 88);
  EXPECT_EQ(client->Current().devices.front().p[1], 2);
  ASSERT_EQ(client->Logs().size(), 1u);
  EXPECT_EQ(client->Logs().front().text, "tracking works");
}

TEST_F(ViewerInput, RejectsStateBeforeGreeting) {
  Message({{"type", "state"}, {"devices", {{{"serial", "not-a-driver"}}}}});
  Rejected();
}

TEST_F(ViewerInput, RejectsFractionalProtocolVersion) {
  Message({{"type", "hello"}, {"name", "openhouse"}, {"protocol", 1.5}});
  Rejected();
}

TEST_F(ViewerInput, RejectsAnUnrelatedService) {
  Message({{"type", "hello"}, {"name", "other-service"}, {"protocol", 1}});
  Rejected();
}

TEST_F(ViewerInput, RejectsCoordinatesThatWouldOverflowTheMap) {
  Hello();
  Message({{"type", "state"}, {"devices", {{{"valid", true}, {"p", {1e300, 0.0, 0.0}}}}}});
  Rejected();
  EXPECT_EQ(client->Pid(), 0);
}

TEST_F(ViewerInput, RejectsOutOfRangeIntegerConversion) {
  Hello();
  Message({{"type", "state"}, {"devices", {{{"battery", 1e300}}}}});
  Rejected();
}

TEST_F(ViewerInput, RejectsOversizedCollections) {
  Hello();
  json devices = json::array();
  for (int i = 0; i < 257; ++i) devices.push_back(json::object());
  Message({{"type", "state"}, {"devices", devices}});
  Rejected();
}

TEST_F(ViewerInput, RejectsMalformedJson) {
  Hello();
  Send("{broken\n");
  Rejected();
}

TEST_F(ViewerInput, AcceptsAValidLineAcrossReadBudgets) {
  Hello();
  Message({{"type", "log"}, {"text", std::string(70000, 'x')}});
  ASSERT_TRUE(client->Connected());
  ASSERT_EQ(client->Logs().size(), 1u);
  EXPECT_EQ(client->Logs().front().text.size(), 70000u);
}

TEST_F(ViewerInput, RejectsAnOversizedMessage) {
  Hello();
  Message({{"type", "log"}, {"text", std::string(270000, 'x')}});
  Rejected();
}

TEST(ViewerSettings, RoundTripsAndRejectsMalformedFieldsWithoutPartialChanges) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  Settings saved;
  saved.detail = Detail::Advanced;
  LogFilter filter;
  filter.hidden.insert("example");
  saved.Save(path, filter);
  Settings loaded;
  LogFilter restored;
  ASSERT_TRUE(loaded.Load(path, restored));
  EXPECT_EQ(loaded.detail, Detail::Advanced);
  EXPECT_EQ(restored.hidden, filter.hidden);
  for (const auto& bad : {R"({"advanced":"yes"})", R"({"logs":[]})",
                          R"({"advanced":true,"logs":{"debug":"wrong"}})", "{broken"}) {
    lh::files::AtomicWrite(path, bad);
    Settings settings;
    LogFilter unchanged;
    unchanged.text = "keep this filter";
    EXPECT_FALSE(settings.Load(path, unchanged));
    EXPECT_EQ(settings.detail, Detail::Basic);
    EXPECT_EQ(unchanged.text, "keep this filter");
  }
}

TEST(ViewerSettings, RefusesOversizedInputAndOutput) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  lh::files::AtomicWrite(path, std::string(Settings::kMaxBytes + 1, ' '));
  Settings settings;
  LogFilter filter;
  EXPECT_FALSE(settings.Load(path, filter));
  filter.hidden.insert(std::string(Settings::kMaxBytes + 1, 'x'));
  EXPECT_THROW(settings.Save(path, filter), std::runtime_error);
  EXPECT_EQ(lh::files::Read(path), std::string(Settings::kMaxBytes + 1, ' '));
}

TEST(ViewerSettings, FailedReplacementPreservesPreviousSettings) {
#ifdef _WIN32
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  const std::string original = R"({"advanced":false})";
  lh::files::AtomicWrite(path, original);
  HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(lock, INVALID_HANDLE_VALUE);
  Settings settings;
  LogFilter filter;
  EXPECT_THROW(settings.Save(path, filter), std::runtime_error);
  CloseHandle(lock);
  EXPECT_EQ(lh::files::Read(path), original);
#else
  GTEST_SKIP() << "Windows sharing-mode failure injection";
#endif
}

TEST(ViewerReport, PublishesOnlyACompletedZip) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  lh::report::ZipWriter writer;
  ASSERT_TRUE(writer.Open(path));
  ASSERT_TRUE(writer.AddText("info.txt", "local diagnostic report"));
  EXPECT_FALSE(fs::exists(path));
  ASSERT_TRUE(writer.Close());
  const auto bytes = lh::files::Read(path);
  EXPECT_EQ(bytes.substr(0, 4), std::string("PK\x03\x04", 4));
  EXPECT_NE(bytes.find("PK\x05\x06"), std::string::npos);
}

TEST(ViewerReport, RefusesAnExistingReportWithoutTruncatingIt) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  lh::files::AtomicWrite(path, "previous report");
  {
    lh::report::ZipWriter writer;
    ASSERT_TRUE(writer.Open(path));
    EXPECT_EQ(lh::files::Read(path), "previous report");
    ASSERT_TRUE(writer.AddText("info.txt", "new report"));
    EXPECT_THROW(writer.Close(), std::runtime_error);
  }
  EXPECT_EQ(lh::files::Read(path), "previous report");
}

TEST(ViewerReport, FailedInputDoesNotLeaveAPartialReport) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const auto path = temp.Payload();
  {
    lh::report::ZipWriter writer;
    ASSERT_TRUE(writer.Open(path));
    EXPECT_FALSE(writer.AddFile("missing.txt", path.parent_path() / "missing.txt"));
  }
  EXPECT_FALSE(fs::exists(path));
  EXPECT_TRUE(fs::is_empty(path.parent_path()));
}
}  // namespace
