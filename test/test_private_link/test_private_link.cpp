#include <gtest/gtest.h>
#include "helpers/PrivateLinkState.h"
#include "helpers/PrivateSetupPolicy.h"
#include "../../examples/simple_repeater/Runtime.h"
#include "../../examples/simple_repeater/SecureTransport.h"
#include <array>

using namespace private_remote;

namespace {
struct Sink : ReceptionSink {
  bool available = true;
  int received = 0;
  bool ready() const override { return available; }
  bool offer(const Reception&) override { ++received; return true; }
};

std::array<uint8_t, 21> group_packet(unsigned width) {
  std::array<uint8_t, 21> raw{};
  raw[0] = (5 << 2) | 1; // Unscoped native flood, group text.
  raw[1] = (width - 1) << 6;
  return raw;
}
}

TEST(PrivateLink, AdmitsTwoAndThreeByteGroupsButKeepsOneByteOnRf) {
  for (unsigned width : {1, 2, 3}) {
    auto raw = group_packet(width);
    EXPECT_EQ(native_peer_payload(raw.data(), raw.size()), width > 1);
    EXPECT_FALSE(native_peer_payload(raw.data(), raw.size() - 1));
  }
}

TEST(PrivateLink, ObserverContinuesWhenInternetSinkIsUnavailable) {
  Runtime runtime;
  Sink observer, link;
  link.available = false;
  runtime.sinks(&observer, &link);
  runtime.capture.config.internet_enabled = true;
  auto raw = group_packet(2);
  ASSERT_TRUE(runtime.capture.hardware_received(raw.data(), raw.size(), 4, -90, 100));
  runtime.tick(101);
  EXPECT_EQ(observer.received, 1);
  EXPECT_EQ(link.received, 0);
  EXPECT_EQ(runtime.stats.link_dropped, 1U);
}

TEST(PrivateLink, CaptureSaturationIsBoundedAndOldPacketsExpire) {
  Runtime runtime;
  Sink observer;
  runtime.sinks(&observer, nullptr);
  auto raw = group_packet(1);
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(runtime.capture.hardware_received(raw.data(), raw.size(), 4, -90, 100));
  EXPECT_FALSE(runtime.capture.hardware_received(raw.data(), raw.size(), 4, -90, 100));
  runtime.tick(5101);
  EXPECT_EQ(runtime.capture.dropped, 1U);
  EXPECT_EQ(runtime.stats.expired, 1U);
  EXPECT_EQ(observer.received, 0);
}

TEST(PrivateLink, SetupRequiresMissingSsidOrDeliberateRecovery) {
  EXPECT_TRUE(wifi_setup_required(false, ""));
  EXPECT_TRUE(wifi_setup_required(true, "saved-network"));
  EXPECT_FALSE(wifi_setup_required(false, "saved-network"));
  uint32_t cookie = setup_cookie_value;
  EXPECT_FALSE(consume_setup_cookie(cookie, false));
  EXPECT_EQ(cookie, 0U);
  cookie = setup_cookie_value;
  EXPECT_TRUE(consume_setup_cookie(cookie, true));
  EXPECT_FALSE(consume_setup_cookie(cookie, true));
}

TEST(PrivateLink, DisconnectInvalidatesAuthenticationGeneration) {
  private_link_available().store(true);
  network_allowed().store(true);
  fresh_time().store(true);
  tls_allocator_ready().store(true);
  mark_peer_authenticated(network_generation().load());
  EXPECT_STREQ(private_link_status(true), "Link: authenticated");
  stop_network();
  EXPECT_FALSE(peer_authenticated().load());
  network_allowed().store(true);
  EXPECT_STREQ(private_link_status(true), "Link: connecting");
}

TEST(PrivateLink, CertificateClockRejectsStaleOrUntrustedTime) {
  SecureConfig config;
  config.trusted_clock = true;
  config.unix_seconds = 1800000000;
  EXPECT_TRUE(tls_clock_consistent(config, 1800000005));
  EXPECT_FALSE(tls_clock_consistent(config, 1800000006));
  config.trusted_clock = false;
  EXPECT_FALSE(tls_clock_consistent(config, 1800000000));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
