#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include "common/test_paths.hpp"
#include "lh/session.hpp"

TEST(PersonalProfile, ReplayRetainsControllerButOmitsHeadset) {
#ifndef LH_TEST_USB
  GTEST_SKIP() << "Replay requires the USB capture layer";
#else
  const auto capture = TestCapture("session-20261016-142931Z-147083");
  ASSERT_TRUE(std::filesystem::exists(capture + "/input.lhraw"));
  lh::SessionOptions options;
  options.replayPath = capture + "/input.lhraw";
  options.replayRealtime = false;
  options.trackHeadsets = false;
  options.saveRoomMap = false;
  options.steamDatabasePath = capture + "/lighthousedb.json";
  auto session = lh::Session::Create(options);
  std::string error;
  ASSERT_TRUE(session->Start(&error)) << error;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!session->Finished() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  EXPECT_TRUE(session->Finished());
  const auto snapshot = session->Snapshot();
  bool controller = false;
  for (const auto& device : snapshot.devices) {
    EXPECT_NE(device.info.role, lh::DeviceRole::Hmd);
    controller |= device.info.serial == "LHR-13DA81D1" &&
                  device.info.role == lh::DeviceRole::Controller;
  }
  EXPECT_TRUE(controller);
  session->Stop();
#endif
}
