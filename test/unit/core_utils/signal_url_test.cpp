#include "signal_url.h"
#include "uri.h"

#include <gtest/gtest.h>

namespace livekit::core::detail {
namespace {

TEST(SignalUrlTest, AppliesConfiguredConnectionParameters) {
	SignalOptions options;
	options.auto_subscribe = false;
	options.adaptive_stream = true;
	options.reconnect = true;
	options.reconnect_reason = 3;
	options.participant_sid = "PA_test+sid";
	options.sdk_options.sdk = "cpp test";
	options.sdk_options.sdk_version = "1.2.3+dev";

	const Url url(BuildSignalUrl("https://example.com/rtc?existing=kept", "token+value", options));
	const auto parameters = url.GetQueryParameters();

	EXPECT_EQ(url.GetScheme(), "https");
	EXPECT_EQ(url.GetHost(), "example.com");
	EXPECT_EQ(url.GetPath(), "rtc");
	EXPECT_EQ(parameters.at("existing"), "kept");
	EXPECT_EQ(parameters.at("access_token"), "token%2Bvalue");
	EXPECT_EQ(parameters.at("auto_subscribe"), "0");
	EXPECT_EQ(parameters.at("adaptive_stream"), "1");
	EXPECT_EQ(parameters.at("reconnect"), "1");
	EXPECT_EQ(parameters.at("reconnect_reason"), "3");
	EXPECT_EQ(parameters.at("sid"), "PA_test%2Bsid");
	EXPECT_EQ(parameters.at("sdk"), "cpp%20test");
	EXPECT_EQ(parameters.at("version"), "1.2.3%2Bdev");
	EXPECT_EQ(parameters.at("protocol"), "17");
	EXPECT_EQ(parameters.at("capabilities"), "CAP_PACKET_TRAILER");
}

TEST(SignalUrlTest, OmitsDisabledOptionalParametersAndUsesSdkDefaults) {
	const Url url(BuildSignalUrl("ws://localhost:7880/rtc", "token", SignalOptions{}));
	const auto parameters = url.GetQueryParameters();

	EXPECT_EQ(parameters.at("auto_subscribe"), "1");
	EXPECT_EQ(parameters.at("sdk"), "cpp");
	EXPECT_EQ(parameters.at("version"), "0.0.1");
	EXPECT_EQ(parameters.at("capabilities"), "CAP_PACKET_TRAILER");
	EXPECT_FALSE(parameters.contains("adaptive_stream"));
	EXPECT_FALSE(parameters.contains("reconnect"));
	EXPECT_FALSE(parameters.contains("reconnect_reason"));
	EXPECT_FALSE(parameters.contains("sid"));
}

TEST(SignalUrlTest, AddsRtcPathToProjectRootUrl) {
	const Url url(BuildSignalUrl("wss://project.livekit.cloud", "token", SignalOptions{}));

	EXPECT_EQ(url.GetScheme(), "wss");
	EXPECT_EQ(url.GetHost(), "project.livekit.cloud");
	EXPECT_EQ(url.GetPath(), "rtc");
}

} // namespace
} // namespace livekit::core::detail
