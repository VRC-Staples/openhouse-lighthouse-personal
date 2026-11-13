#include <gtest/gtest.h>
#include <atomic>
#include "platform/platform.hpp"
#include "platform/safe_files.hpp"

TEST(PlatformReview, ParsesUnicodeAndRejectsMalformedPathLists) {
  using lh::platform::JsonStringList;
  EXPECT_EQ(JsonStringList(R"({"runtime":["D:\\VR\u0020Library\\SteamVR","other"]})", "runtime"),
            (std::vector<std::string>{R"(D:\VR Library\SteamVR)", "other"}));
  EXPECT_EQ(JsonStringList(R"({"runtime":["\ud83d\ude00"]})", "runtime"),
            (std::vector<std::string>{"\xf0\x9f\x98\x80"}));
  EXPECT_TRUE(JsonStringList(R"({"runtime":["ok",false]})", "runtime").empty());
  EXPECT_TRUE(JsonStringList(R"({"runtime":"wrong"})", "runtime").empty());
  EXPECT_TRUE(JsonStringList("{broken", "runtime").empty());
}

TEST(PlatformReview, ReleasesTimersWhenThreadsExit) {
#ifdef _WIN32
  auto sleep = [] { lh::platform::SleepFor(std::chrono::microseconds(1000)); };
  std::thread(sleep).join();
  DWORD before = 0, after = 0;
  ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
  for (int i = 0; i < 32; ++i) std::thread(sleep).join();
  ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
  EXPECT_LE(after, before + 2) << "one leaked timer per finished thread";
#else
  GTEST_SKIP() << "Windows timer ownership";
#endif
}

TEST(PlatformReview, ConcurrentBackupsPublishOneCompleteSnapshot) {
  namespace fs = std::filesystem;
  lh::files::StagingDirectory source(fs::temp_directory_path()), backup(fs::temp_directory_path());
  const std::string original(131072, 'x');
  lh::files::AtomicWrite(source.Payload(), original);
  std::atomic<int> errors{0};
  auto copy = [&] {
    try { lh::files::BackupOnce(source.Payload(), backup.Payload()); }
    catch (...) { ++errors; }
  };
  std::thread a(copy), b(copy);
  a.join(); b.join();
  EXPECT_EQ(errors, 0);
  EXPECT_EQ(lh::files::Read(backup.Payload()), original);
  lh::files::AtomicWrite(source.Payload(), "different original");
  EXPECT_THROW(lh::files::BackupOnce(source.Payload(), backup.Payload()), std::runtime_error);
  EXPECT_EQ(lh::files::Read(backup.Payload()), original);
}
