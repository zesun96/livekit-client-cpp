#include "../../src/core/track/track.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace livekit::core {
namespace {

TEST(TrackStatsTest, UsesProviderAndHandlesUnavailableStats) {
	Track track("TR_test", "test", TrackKind::Video);
	EXPECT_TRUE(track.GetRTCStats().empty());

	track.SetStatsProvider([] { return R"([{"id":"outbound","type":"outbound-rtp"}])"; });
	EXPECT_NE(track.GetRTCStats().find("outbound-rtp"), std::string::npos);
	const auto snapshot = track.GetRTCStatsSnapshot();
	ASSERT_EQ(snapshot.streams.size(), 1u);
	EXPECT_EQ(snapshot.streams[0].direction, RTCStatsDirection::Send);

	track.SetStatsProvider([]() -> std::string { throw std::runtime_error("stats failed"); });
	EXPECT_TRUE(track.GetRTCStats().empty());

	track.SetStatsProvider({});
	EXPECT_TRUE(track.GetRTCStats().empty());
}

TEST(TrackStatsTest, NormalizesRtpStreamsAndReferencedStats) {
	const auto snapshot = ParseRTCStatsReport(R"([
        {"id":"codec","type":"codec","mimeType":"video/VP8"},
        {"id":"remote","type":"remote-inbound-rtp","jitter":0.004,
         "roundTripTime":0.025},
        {"id":"send","type":"outbound-rtp","kind":"video","rid":"h",
         "timestamp":1000,"bytesSent":12000,"packetsSent":120,"packetsLost":2,
         "frameWidth":1280,"frameHeight":720,"framesPerSecond":30,"framesSent":300,
         "qpSum":400,"codecId":"codec","remoteId":"remote",
         "encoderImplementation":"libvpx","qualityLimitationReason":"none"},
        {"id":"receive","type":"inbound-rtp","mediaType":"audio","timestamp":1000,
         "bytesReceived":5000,"packetsReceived":50,"packetsLost":-1,"jitter":0.003,
         "audioLevel":0.4,"concealedSamples":12}
    ])");

	ASSERT_EQ(snapshot.streams.size(), 2u);
	const auto& send = snapshot.streams[0];
	EXPECT_EQ(send.direction, RTCStatsDirection::Send);
	EXPECT_EQ(send.kind, "video");
	EXPECT_EQ(send.rid, "h");
	EXPECT_EQ(send.bytes, 12000u);
	EXPECT_EQ(send.frame_width, 1280u);
	EXPECT_EQ(send.codec_mime_type, "video/VP8");
	EXPECT_EQ(send.codec_implementation, "libvpx");
	ASSERT_TRUE(send.round_trip_time_seconds.has_value());
	EXPECT_DOUBLE_EQ(*send.round_trip_time_seconds, 0.025);
	ASSERT_TRUE(send.jitter_seconds.has_value());
	EXPECT_DOUBLE_EQ(*send.jitter_seconds, 0.004);

	const auto& receive = snapshot.streams[1];
	EXPECT_EQ(receive.direction, RTCStatsDirection::Receive);
	EXPECT_EQ(receive.kind, "audio");
	EXPECT_EQ(receive.packets_lost, -1);
	EXPECT_EQ(receive.concealed_samples, 12u);
	ASSERT_TRUE(receive.audio_level.has_value());
	EXPECT_DOUBLE_EQ(*receive.audio_level, 0.4);
}

TEST(TrackStatsTest, ComputesBitrateAcrossSamplesAndResetsDiscontinuities) {
	RTCStatsMonitor monitor;
	auto first = monitor.Sample(
	    R"([{"id":"send","type":"outbound-rtp","timestamp":1000,"bytesSent":1000}])");
	ASSERT_EQ(first.streams.size(), 1u);
	EXPECT_FALSE(first.streams[0].bitrate_bps.has_value());
	EXPECT_TRUE(monitor.Sample("temporarily unavailable").Empty());

	auto second = monitor.Sample(
	    R"([{"id":"send","type":"outbound-rtp","timestamp":3000,"bytesSent":6000}])");
	ASSERT_EQ(second.streams.size(), 1u);
	ASSERT_TRUE(second.streams[0].bitrate_bps.has_value());
	EXPECT_DOUBLE_EQ(*second.streams[0].bitrate_bps, 20000.0);

	auto reset =
	    monitor.Sample(R"([{"id":"send","type":"outbound-rtp","timestamp":4000,"bytesSent":10}])");
	ASSERT_EQ(reset.streams.size(), 1u);
	EXPECT_FALSE(reset.streams[0].bitrate_bps.has_value());

	monitor.Reset();
	auto after_reset =
	    monitor.Sample(R"([{"id":"send","type":"outbound-rtp","timestamp":5000,"bytesSent":100}])");
	EXPECT_FALSE(after_reset.streams[0].bitrate_bps.has_value());
}

TEST(TrackStatsTest, RejectsMalformedReportsWithoutThrowing) {
	EXPECT_TRUE(ParseRTCStatsReport("not json").Empty());
	EXPECT_TRUE(ParseRTCStatsReport(R"({"type":"outbound-rtp"})").Empty());
}

} // namespace
} // namespace livekit::core
