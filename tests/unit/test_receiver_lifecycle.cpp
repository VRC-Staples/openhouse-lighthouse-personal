#include <gtest/gtest.h>
#include "transport/usb/usb_transport.hpp"

namespace lh::usb {
// Inject only reports and a synthetic endpoint; never enumerate or open USB devices.
struct UsbTransportTestAccess {
  static void CheckReconnect() {
    struct Sink : InputSinkBase {
      UsbTransport* transport = nullptr;
      void OnDeviceDetached(const DeviceId& id) override { transport->OnDeviceGone(id); }
      void OnTransportLog(LogLevel, const std::string&) override {}
    } sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    sink.transport = &transport;
    UsbTransport::Open endpoint;
    endpoint.info.ep = 1;
    endpoint.info.kind = EndpointKind::Wireless;
    endpoint.info.serial = "LHR-synthetic";
    endpoint.receiver = "test-dongle";
    transport.decoder_.AddEndpoint(endpoint.info);
    transport.SetReceiver(endpoint, endpoint.info.serial);
    const uint8_t off[] = {0x26}, data[] = {0x23};
    auto connected = [&] {
      const auto receivers = transport.Receivers();
      EXPECT_EQ(receivers.size(), 1u);
      return receivers.empty() ? std::string() : receivers[0].connected;
    };
    EXPECT_EQ(connected(), endpoint.info.serial);
    transport.ProcessReport(endpoint, 1.0, true, off, sizeof off);
    EXPECT_TRUE(connected().empty());
    transport.ProcessReport(endpoint, 1.1, true, data, sizeof data);
    EXPECT_EQ(connected(), endpoint.info.serial);
    // A missed off notification resets and reconnects within this same report.
    transport.ProcessReport(endpoint, 3.0, true, data, sizeof data);
    EXPECT_EQ(connected(), endpoint.info.serial);
  }

  static void CheckPairingCommandReplacement() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    transport.receivers_["test-first"] = {"test-first", "", 0};
    transport.receivers_["test-second"] = {"test-second", "", 0};
    ASSERT_TRUE(transport.SetPairing("test-first", 1000));
    ASSERT_TRUE(transport.SetPairing("test-first", 0));
    ASSERT_EQ(transport.pairing_.size(), 1u);
    EXPECT_EQ(transport.pairing_[0].milliseconds, 0);

    ASSERT_TRUE(transport.SetPairing("test-first", 2000));
    ASSERT_TRUE(transport.SetPairing("test-first", 3000));
    ASSERT_EQ(transport.pairing_.size(), 1u);
    EXPECT_EQ(transport.pairing_[0].milliseconds, 3000);

    ASSERT_TRUE(transport.SetPairing("test-second", 4000));
    ASSERT_TRUE(transport.SetPairing("test-first", 5000));
    ASSERT_EQ(transport.pairing_.size(), 2u);
    EXPECT_EQ(transport.pairing_[0].receiver, "test-second");
    EXPECT_EQ(transport.pairing_[0].milliseconds, 4000);
    EXPECT_EQ(transport.pairing_[1].receiver, "test-first");
    EXPECT_EQ(transport.pairing_[1].milliseconds, 5000);
  }

  static void CheckPairingBoundsAndUnknownReceiver() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    transport.receivers_["test-dongle"] = {"test-dongle", "", 0};
    EXPECT_FALSE(transport.SetPairing("not-a-receiver", 1000));
    EXPECT_TRUE(transport.pairing_.empty());
    ASSERT_TRUE(transport.SetPairing("test-dongle", -1));
    ASSERT_EQ(transport.pairing_.size(), 1u);
    EXPECT_EQ(transport.pairing_[0].milliseconds, 0);
    ASSERT_TRUE(transport.SetPairing("test-dongle", 100000));
    ASSERT_EQ(transport.pairing_.size(), 1u);
    EXPECT_EQ(transport.pairing_[0].milliseconds, 0xffff);
    EXPECT_FALSE(transport.SetPairing("not-a-receiver", 0));
    ASSERT_EQ(transport.pairing_.size(), 1u);
    EXPECT_EQ(transport.pairing_[0].milliseconds, 0xffff);
  }
};
}  // namespace lh::usb

TEST(ReceiverLifecycle, RestoresOccupancyAfterPowerCycleAndRadioGap) {
  lh::usb::UsbTransportTestAccess::CheckReconnect();
}

TEST(ReceiverLifecycle, LatestPairingCommandReplacesOnlyThatReceiversPendingCommand) {
  lh::usb::UsbTransportTestAccess::CheckPairingCommandReplacement();
}

TEST(ReceiverLifecycle, PairingBoundsAndUnknownReceiversPreserveTheQueue) {
  lh::usb::UsbTransportTestAccess::CheckPairingBoundsAndUnknownReceiver();
}
