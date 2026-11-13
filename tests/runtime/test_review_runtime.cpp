#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <limits>
#include <thread>
#include <nlohmann/json.hpp>
#include "common/test_paths.hpp"
#include "lh/input_sink.hpp"
#include "lh/session.hpp"
#include "platform/safe_files.hpp"
#include "runtime/mapped_input.hpp"
#ifdef LH_TEST_USB
#include "transport/usb/raw_capture.hpp"
#endif

TEST(MappedBounds, RejectsOffsetsAndWidthsBeforeArithmetic) {
  using namespace lh::runtime;
  MappedComponent component;
  component.kind = MappedComponent::Kind::Scalar;
  component.bitOffset = std::numeric_limits<int>::max();
  component.bitWidth = 8;
  component.max = 255;
  EXPECT_FALSE(MappedValue({0xff}, component));
  component.bitOffset = 7; component.bitWidth = 2;
  EXPECT_FALSE(MappedValue({0xff}, component));
  component.bitOffset = 0; component.bitWidth = 33;
  EXPECT_FALSE(MappedValue({0xff}, component));
  component.bitOffset = 1; component.bitWidth = 8;
  ASSERT_TRUE(MappedValue({0xfe, 1}, component));
  EXPECT_EQ(*MappedValue({0xfe, 1}, component), 1.0);
  EXPECT_FALSE(MappedValue({}, component));
}

TEST(MappedBounds, RejectsFractionalAndOutOfRangeProfileIntegers) {
  using json = nlohmann::json;
  for (const json value : {json(1.5), json(1e300), json(UINT64_MAX), json("1")}) {
    const json profile = {{"input_components", {{"/input/a/click",
        {{"type", "scalar"}, {"bit_width", 8}, {"bit_offset", value}}}}}};
    lh::runtime::MappedProfile parsed;
    std::string error;
    EXPECT_FALSE(lh::runtime::ParseMappedProfile(profile.dump(), "", &parsed, &error));
  }
}

TEST(DeviceSetup, FailedReplacementPreservesConfigurationAndRetries) {
  const auto path = TestCapture("session-20261016-142931Z-147083/device-configs/device-1.json");
  std::ifstream file(path);
  ASSERT_TRUE(file);
  nlohmann::json good; file >> good;
  good["registered_device_type"] = "original-type";
  lh::SessionOptions options;
  options.saveRoomMap = false;
  options.preinitDirectory = "-";
  options.steamDatabasePath = "-";
  auto session = lh::Session::Create(options);
  auto* sink = dynamic_cast<lh::IInputSink*>(session.get());
  ASSERT_NE(sink, nullptr);
  const std::string original = good.dump();
  sink->OnDeviceAttached("synthetic", original);
  ASSERT_EQ(session->Snapshot().devices.size(), 1u);
  ASSERT_EQ(session->Snapshot().devices.front().info.configJson, original);
  session->PollEvents();
  auto bad = good; bad["registered_device_type"] = nlohmann::json::array();
  sink->OnDeviceAttached("synthetic", bad.dump());
  sink->OnDeviceAttached("synthetic", bad.dump());
  EXPECT_EQ(session->Snapshot().devices.front().info.configJson, original);
  int failures = 0;
  for (const auto& event : session->PollEvents())
    if (event.text.find("unusable device configuration") != std::string::npos) ++failures;
  EXPECT_EQ(failures, 2);
  good["registered_device_type"] = "replacement-type";
  sink->OnDeviceAttached("synthetic", good.dump());
  EXPECT_EQ(session->Snapshot().devices.front().info.registeredType, "replacement-type");
}

#ifdef LH_TEST_USB
TEST(ReplayLifecycle, StopInterruptsLongRecordedGaps) {
  lh::files::StagingDirectory temp(std::filesystem::temp_directory_path());
  {
    lh::usb::RawCaptureWriter writer;
    ASSERT_TRUE(writer.Open(temp.Payload().string()));
    const uint8_t byte = 0;
    writer.WriteReport(1, 1, true, &byte, 1);
    writer.WriteReport(3, 1, true, &byte, 1);
  }
  lh::SessionOptions options;
  options.replayPath = temp.Payload().string();
  options.saveRoomMap = false;
  options.preinitDirectory = "-";
  options.steamDatabasePath = "-";
  auto session = lh::Session::Create(options);
  std::string error;
  ASSERT_TRUE(session->Start(&error)) << error;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (session->HostNow() < 1 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_GE(session->HostNow(), 1);
  const auto start = std::chrono::steady_clock::now();
  session->Stop();
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
  lh::usb::RawCaptureReader reader;
  ASSERT_TRUE(reader.Open(temp.Payload().string()));
  int reports = 0;
  EXPECT_TRUE(reader.ReadAll([](const auto&) {}, [&](const auto&) { ++reports; },
                            [&] { return reports == 1; }));
  EXPECT_EQ(reports, 1);
}
#endif
