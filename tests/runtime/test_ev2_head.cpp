#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "common/test_paths.hpp"
#include "lh/input_sink.hpp"
#include "lh/session.hpp"

namespace {

nlohmann::json ControllerConfig(const std::string& name,
                                const std::array<double, 3>& position) {
  std::ifstream file(TestCapture(
      "session-20261016-142931Z-147083/device-configs/device-1.json"));
  auto config = nlohmann::json::parse(file);
  config["model_number"] = name;
  config["device_class"] = "controller";
  for (const auto* key : {"imu", "head"}) {
    config[key]["plus_x"] = {1, 0, 0};
    config[key]["plus_z"] = {0, 0, 1};
    config[key]["position"] = {0, 0, 0};
  }
  config["head"]["position"] = position;
  return config;
}

TEST(DeviceSetup, EV2HeadOffsetsUseTheIncomingModelAndPreserveExplicitPositions) {
  struct Case {
    const char* model;
    std::array<double, 3> supplied;
    std::array<double, 3> expected;
  };
  const Case cases[] = {
      {"Knuckles EV2.0 Left", {}, {-0.010757, 0.021781, -0.04214}},
      {"Knuckles EV2.0 Right", {}, {0.010757, 0.021781, -0.04214}},
      {"Knuckles EV2.0 Right", {0.1, 0.2, 0.3}, {0.1, 0.2, 0.3}},
      {"Knuckles EV3 Right", {}, {}},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.model);
    lh::SessionOptions options;
    options.saveRoomMap = false;
    options.preinitDirectory = options.steamDatabasePath = "-";
    auto session = lh::Session::Create(options);
    auto* sink = dynamic_cast<lh::IInputSink*>(session.get());
    ASSERT_NE(sink, nullptr);
    sink->OnDeviceAttached("synthetic-ev2", ControllerConfig(test.model, test.supplied).dump());
    const auto snapshot = session->Snapshot();
    ASSERT_EQ(snapshot.devices.size(), 1u);
    const auto& position = snapshot.devices.front().pose.imuFromHead.position;
    for (size_t axis = 0; axis < position.size(); ++axis)
      EXPECT_NEAR(position[axis], test.expected[axis], 1e-9);
  }
}

}  // namespace
