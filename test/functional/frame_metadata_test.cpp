#include "frame_metadata.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace livekit::core::detail {
namespace {

TEST(FrameMetadataTest, PacketTrailerRoundTripsAllFieldsAndMatchesWireEnvelope) {
	const std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
	VideoFrameMetadata metadata;
	metadata.user_timestamp_us = 1'744'249'600'123'456ULL;
	metadata.frame_id = 42;
	metadata.user_data = std::vector<std::uint8_t>{0x00, 0x01, 0xfe, 0xff, 0x42};
	FrameMetadataFeatures features{true, true, true};

	const auto encoded = AppendFrameMetadataTrailer(payload, metadata, features);
	ASSERT_GT(encoded.size(), payload.size());
	ASSERT_GE(encoded.size(), 5u);
	EXPECT_EQ(std::vector<std::uint8_t>(encoded.end() - 4, encoded.end()),
	          (std::vector<std::uint8_t>{'L', 'K', 'T', 'S'}));
	EXPECT_EQ(static_cast<std::size_t>(encoded[encoded.size() - 5] ^ 0xffU),
	          encoded.size() - payload.size());

	const auto decoded = ExtractFrameMetadataTrailer(encoded);
	EXPECT_EQ(decoded.payload, payload);
	ASSERT_TRUE(decoded.metadata.has_value());
	EXPECT_EQ(decoded.metadata->user_timestamp_us, metadata.user_timestamp_us);
	EXPECT_EQ(decoded.metadata->frame_id, metadata.frame_id);
	EXPECT_EQ(decoded.metadata->user_data, metadata.user_data);
}

TEST(FrameMetadataTest, PublishFeaturesFilterFields) {
	const std::vector<std::uint8_t> payload{0xaa, 0xbb};
	VideoFrameMetadata metadata;
	metadata.user_timestamp_us = 99;
	metadata.frame_id = 7;
	metadata.user_data = std::vector<std::uint8_t>{1, 2, 3};

	const auto encoded =
	    AppendFrameMetadataTrailer(payload, metadata, FrameMetadataFeatures{false, true, false});
	const auto decoded = ExtractFrameMetadataTrailer(encoded);
	ASSERT_TRUE(decoded.metadata.has_value());
	EXPECT_FALSE(decoded.metadata->user_timestamp_us.has_value());
	EXPECT_EQ(decoded.metadata->frame_id, 7u);
	EXPECT_FALSE(decoded.metadata->user_data.has_value());
}

TEST(FrameMetadataTest, RejectsMalformedOrOversizedMetadataWithoutDamagingPayload) {
	const std::vector<std::uint8_t> payload{0x10, 0x20, 0x30};
	EXPECT_EQ(ExtractFrameMetadataTrailer(payload).payload, payload);

	auto malformed = payload;
	malformed.insert(malformed.end(), {0xfe, 0xf7, 0xff, 'L', 'K', 'T', 'S'});
	EXPECT_EQ(ExtractFrameMetadataTrailer(malformed).payload, malformed);

	VideoFrameMetadata oversized;
	oversized.user_data = std::vector<std::uint8_t>(kMaxVideoFrameMetadataUserDataSize + 1, 0x7f);
	EXPECT_FALSE(IsValidVideoFrameMetadata(oversized));
	EXPECT_EQ(
	    AppendFrameMetadataTrailer(payload, oversized, FrameMetadataFeatures{false, false, true}),
	    payload);
}

TEST(FrameMetadataTest, RtpTimestampUsesVideoClockAndWrapsTo32Bits) {
	EXPECT_EQ(VideoRtpTimestampFromMicros(1'000'000), 90'000u);
	EXPECT_EQ(VideoRtpTimestampFromMicros(1'000'011), 90'000u);
}

TEST(FrameMetadataTest, StoreIsBoundedAndKeepsRecentEntries) {
	FrameMetadataStore store;
	for (std::uint32_t index = 0; index < 300; ++index) {
		VideoFrameMetadata metadata;
		metadata.frame_id = index;
		store.Store(index, std::move(metadata));
	}
	EXPECT_FALSE(store.Find(0).has_value());
	ASSERT_TRUE(store.Find(299).has_value());
	EXPECT_EQ(store.Find(299)->frame_id, 299u);
}

TEST(FrameMetadataTest, StoreCanFindMetadataByCaptureTimestampAfterRtpTimestampChanges) {
	FrameMetadataStore store;
	VideoFrameMetadata metadata;
	metadata.frame_id = 42;
	store.Store(92'999, metadata, 1'033'333);

	EXPECT_FALSE(store.Find(123'456).has_value());
	const auto found = store.FindByCaptureTimestamp(1'033'000);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->frame_id, 42);
}

} // namespace
} // namespace livekit::core::detail
