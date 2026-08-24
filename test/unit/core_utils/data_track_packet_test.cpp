#include "data_track_packet.h"

#include <gtest/gtest.h>

#include <array>

namespace livekit::core::detail {
namespace {

TEST(DataTrackPacketTest, SerializesBigEndianWireHeader) {
	DataTrackPacket packet;
	packet.marker = DataTrackFrameMarker::Single;
	packet.track_handle = 0x1234;
	packet.sequence = 0x5678;
	packet.frame_number = 0x9abc;
	packet.timestamp = 0xdef01234;
	packet.payload = {0xaa, 0xbb};

	auto bytes = SerializeDataTrackPacket(packet);
	ASSERT_TRUE(bytes.has_value());
	const std::vector<uint8_t> expected = {0x18, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a,
	                                       0xbc, 0xde, 0xf0, 0x12, 0x34, 0xaa, 0xbb};
	EXPECT_EQ(*bytes, expected);
	auto decoded = DeserializeDataTrackPacket(bytes->data(), bytes->size());
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(decoded->marker, DataTrackFrameMarker::Single);
	EXPECT_EQ(decoded->track_handle, packet.track_handle);
	EXPECT_EQ(decoded->payload, packet.payload);
}

TEST(DataTrackPacketTest, RoundTripsExtensions) {
	DataTrackPacket packet;
	packet.track_handle = 7;
	packet.extensions.key_index = 3;
	packet.extensions.iv = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	packet.extensions.user_timestamp = 0x0102030405060708ULL;
	packet.payload = {4, 5, 6};
	auto bytes = SerializeDataTrackPacket(packet);
	ASSERT_TRUE(bytes.has_value());
	auto decoded = DeserializeDataTrackPacket(bytes->data(), bytes->size());
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(decoded->extensions.key_index, 3);
	EXPECT_EQ(decoded->extensions.iv, packet.extensions.iv);
	EXPECT_EQ(decoded->extensions.user_timestamp, packet.extensions.user_timestamp);
	EXPECT_EQ(decoded->payload, packet.payload);
}

TEST(DataTrackPacketTest, ReassemblesUnorderedFragments) {
	DataTrackFrame frame;
	for (uint8_t value = 0; value < 20; ++value) {
		frame.payload.push_back(value);
	}
	frame.user_timestamp = 42;
	DataTrackPacketizer packetizer(9, 28);
	DataTrackPacketExtensions extensions;
	extensions.user_timestamp = frame.user_timestamp;
	auto packets = packetizer.Packetize(frame, extensions);
	ASSERT_TRUE(packets.has_value());
	ASSERT_EQ(packets->size(), 5u);

	std::array<std::size_t, 5> order{4, 0, 2, 1, 3};
	DataTrackDepacketizer depacketizer(2);
	std::optional<DataTrackAssembledFrame> assembled;
	for (const auto index : order) {
		auto packet =
		    DeserializeDataTrackPacket((*packets)[index].data(), (*packets)[index].size());
		ASSERT_TRUE(packet.has_value());
		auto result = depacketizer.Push(std::move(*packet));
		if (result) {
			assembled = std::move(result);
		}
	}
	ASSERT_TRUE(assembled.has_value());
	EXPECT_EQ(assembled->frame.payload, frame.payload);
	EXPECT_EQ(assembled->frame.user_timestamp, frame.user_timestamp);
}

TEST(DataTrackPacketTest, RejectsMalformedPacketsAndBoundsPartialFrames) {
	const std::vector<uint8_t> too_short(11, 0);
	EXPECT_FALSE(DeserializeDataTrackPacket(too_short.data(), too_short.size()));
	std::vector<uint8_t> bad_extension = {0x1c, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
	EXPECT_FALSE(DeserializeDataTrackPacket(bad_extension.data(), bad_extension.size()));

	DataTrackDepacketizer depacketizer(1);
	DataTrackPacket first;
	first.marker = DataTrackFrameMarker::Start;
	first.track_handle = 1;
	first.frame_number = 1;
	first.payload = {1};
	EXPECT_FALSE(depacketizer.Push(first));
	first.frame_number = 2;
	EXPECT_FALSE(depacketizer.Push(first));
	DataTrackPacket final;
	final.marker = DataTrackFrameMarker::Final;
	final.track_handle = 1;
	final.frame_number = 1;
	final.sequence = 1;
	final.payload = {2};
	EXPECT_FALSE(depacketizer.Push(final));
}

} // namespace
} // namespace livekit::core::detail
