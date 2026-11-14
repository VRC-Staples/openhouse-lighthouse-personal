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

  static void CheckChangedDeviceReply() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    UsbTransport::Open endpoint;
    endpoint.info.kind = EndpointKind::Wireless;
    endpoint.path = "synthetic-receiver-path";
    endpoint.lastData = 1.0;
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 2.1, 0x23));
    ASSERT_TRUE(endpoint.checkPending);
    ASSERT_EQ(transport.deviceChecks_.size(), 1u);
    transport.runningChecks_.at(endpoint.path)->result = 1;
    EXPECT_TRUE(transport.CheckReceiverDevice(endpoint, 2.2, 0x23));
    EXPECT_FALSE(endpoint.checkPending);
    EXPECT_TRUE(transport.runningChecks_.empty());
  }

  static void CheckIdentityRetry() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    UsbTransport::Open endpoint;
    endpoint.info.kind = EndpointKind::Wireless;
    endpoint.path = "synthetic-receiver-path";
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.0, 0x26));
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.1, 0x23));
    transport.runningChecks_.at(endpoint.path)->result = -1;
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.2, 0x23));
    EXPECT_FALSE(endpoint.checkPending);
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.3, 0x23));
    EXPECT_FALSE(endpoint.checkPending);
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.5, 0x23));
    ASSERT_TRUE(endpoint.checkPending);
    transport.runningChecks_.at(endpoint.path)->result = 0;
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.6, 0x23));
    EXPECT_FALSE(endpoint.checkDevice);
  }

  static void CheckOldEpochReplyIsIgnored() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    UsbTransport::Open endpoint;
    endpoint.info.kind = EndpointKind::Wireless;
    endpoint.path = "synthetic-receiver-path";
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.0, 0x26));
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.1, 0x23));
    auto oldCheck = transport.runningChecks_.at(endpoint.path);
    oldCheck->result = 1;
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.2, 0x26));
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.3, 0x23));
    auto newCheck = transport.runningChecks_.at(endpoint.path);
    EXPECT_NE(oldCheck, newCheck);
    newCheck->result = 0;
    EXPECT_FALSE(transport.CheckReceiverDevice(endpoint, 1.4, 0x23));
    EXPECT_FALSE(endpoint.checkDevice);
  }

  static void CheckStopReleasesPendingIdentityChecks() {
    InputSinkBase sink;
    UsbTransport transport(&sink, UsbTransportOptions{"unused-test-config"});
    auto check = std::make_shared<UsbTransport::DeviceCheck>();
    check->path = "synthetic-receiver-path";
    const std::weak_ptr<UsbTransport::DeviceCheck> lifetime = check;
    transport.deviceChecks_.push_back(check);
    transport.runningChecks_[check->path] = check;
    check.reset();
    transport.Stop();
    EXPECT_TRUE(lifetime.expired());
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

TEST(ReceiverLifecycle, StopReleasesPendingIdentityChecksBeforeRestart) {
  lh::usb::UsbTransportTestAccess::CheckStopReleasesPendingIdentityChecks();
}

TEST(ReceiverLifecycle, ChangedIdentityReplyRequestsReopenWithoutRunningTheProbe) {
  lh::usb::UsbTransportTestAccess::CheckChangedDeviceReply();
}

TEST(ReceiverLifecycle, UnknownIdentityRepliesRetryAndASameDeviceReplyFinishesTheCheck) {
  lh::usb::UsbTransportTestAccess::CheckIdentityRetry();
}

TEST(ReceiverLifecycle, NewRadioEpochDiscardsAnOldChangedDeviceReply) {
  lh::usb::UsbTransportTestAccess::CheckOldEpochReplyIsIgnored();
}
