#pragma once

#ifndef _LKC_CORE_DETAIL_DATA_TRACK_PACKET_H_
#define _LKC_CORE_DETAIL_DATA_TRACK_PACKET_H_

#include "livekit/core/data_track.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace livekit {
namespace core {
namespace detail {

enum class DataTrackFrameMarker : uint8_t {
	Intermediate,
	Final,
	Start,
	Single,
};

struct DataTrackPacketExtensions {
	std::optional<uint64_t> user_timestamp;
	std::optional<uint8_t> key_index;
	std::vector<uint8_t> iv;
};

struct DataTrackPacket {
	DataTrackFrameMarker marker = DataTrackFrameMarker::Single;
	uint16_t track_handle = 0;
	uint16_t sequence = 0;
	uint16_t frame_number = 0;
	uint32_t timestamp = 0;
	DataTrackPacketExtensions extensions;
	std::vector<uint8_t> payload;
};

struct DataTrackAssembledFrame {
	DataTrackFrame frame;
	DataTrackPacketExtensions extensions;
};

std::optional<std::vector<uint8_t>> SerializeDataTrackPacket(const DataTrackPacket& packet);
std::optional<DataTrackPacket> DeserializeDataTrackPacket(const uint8_t* data, std::size_t size);

class DataTrackPacketizer {
public:
	explicit DataTrackPacketizer(uint16_t track_handle, std::size_t mtu = 16'000);
	std::optional<std::vector<std::vector<uint8_t>>>
	Packetize(const DataTrackFrame& frame,
	          const std::optional<DataTrackPacketExtensions>& extensions);

private:
	uint16_t track_handle_;
	std::size_t mtu_;
	uint16_t sequence_ = 0;
	uint16_t frame_number_ = 0;
	uint32_t timestamp_base_ = 0;
	std::chrono::steady_clock::time_point epoch_;
};

class DataTrackDepacketizer {
public:
	explicit DataTrackDepacketizer(std::size_t max_partial_frames = 1);
	std::optional<DataTrackAssembledFrame> Push(DataTrackPacket packet);
	void SetMaximumPartialFrames(std::size_t maximum);
	void Reset();

private:
	struct PartialFrame {
		std::optional<uint16_t> start_sequence;
		std::optional<uint16_t> end_sequence;
		DataTrackPacketExtensions extensions;
		std::map<uint16_t, std::vector<uint8_t>> payloads;
	};

	std::optional<DataTrackAssembledFrame> Finish(uint16_t frame_number);
	void EnsureCapacity(uint16_t incoming_frame_number);

	std::size_t max_partial_frames_;
	std::map<uint16_t, PartialFrame> partials_;
	std::vector<uint16_t> insertion_order_;
};

} // namespace detail
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_DATA_TRACK_PACKET_H_
