#include "data_track_packet.h"

#include <algorithm>
#include <limits>
#include <random>

namespace {
constexpr uint8_t kSupportedVersion = 0;
constexpr uint8_t kVersionShift = 5;
constexpr uint8_t kMarkerShift = 3;
constexpr uint8_t kExtensionFlag = 1U << 2;
constexpr std::size_t kBaseHeaderSize = 12;
constexpr uint8_t kE2eeExtensionTag = 1;
constexpr uint8_t kUserTimestampExtensionTag = 2;
constexpr std::size_t kMaximumPacketsPerFrame = 128;

void AppendU16(std::vector<uint8_t>& output, uint16_t value) {
	output.push_back(static_cast<uint8_t>(value >> 8));
	output.push_back(static_cast<uint8_t>(value));
}

void AppendU32(std::vector<uint8_t>& output, uint32_t value) {
	output.push_back(static_cast<uint8_t>(value >> 24));
	output.push_back(static_cast<uint8_t>(value >> 16));
	output.push_back(static_cast<uint8_t>(value >> 8));
	output.push_back(static_cast<uint8_t>(value));
}

void AppendU64(std::vector<uint8_t>& output, uint64_t value) {
	for (int shift = 56; shift >= 0; shift -= 8) {
		output.push_back(static_cast<uint8_t>(value >> shift));
	}
}

uint16_t ReadU16(const uint8_t* data) {
	return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t ReadU32(const uint8_t* data) {
	return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
	       (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

uint64_t ReadU64(const uint8_t* data) {
	uint64_t value = 0;
	for (std::size_t index = 0; index < 8; ++index) {
		value = (value << 8) | data[index];
	}
	return value;
}

std::vector<uint8_t>
SerializeExtensions(const livekit::core::detail::DataTrackPacketExtensions& extensions) {
	std::vector<uint8_t> output;
	if (extensions.key_index && extensions.iv.size() == 12) {
		output.push_back(kE2eeExtensionTag);
		output.push_back(13);
		output.push_back(*extensions.key_index);
		output.insert(output.end(), extensions.iv.begin(), extensions.iv.end());
	}
	if (extensions.user_timestamp) {
		output.push_back(kUserTimestampExtensionTag);
		output.push_back(8);
		AppendU64(output, *extensions.user_timestamp);
	}
	return output;
}
} // namespace

namespace livekit {
namespace core {
namespace detail {

std::optional<std::vector<uint8_t>> SerializeDataTrackPacket(const DataTrackPacket& packet) {
	if (packet.track_handle == 0 ||
	    (packet.extensions.key_index && packet.extensions.iv.size() != 12)) {
		return std::nullopt;
	}
	auto extension_bytes = SerializeExtensions(packet.extensions);
	const std::size_t extension_words =
	    extension_bytes.empty() ? 0 : (2 + extension_bytes.size() + 3) / 4;
	const std::size_t header_size = kBaseHeaderSize + extension_words * 4;
	std::vector<uint8_t> output;
	output.reserve(header_size + packet.payload.size());
	uint8_t initial = (kSupportedVersion << kVersionShift) |
	                  (static_cast<uint8_t>(packet.marker) << kMarkerShift);
	if (!extension_bytes.empty()) {
		initial |= kExtensionFlag;
	}
	output.push_back(initial);
	output.push_back(0);
	AppendU16(output, packet.track_handle);
	AppendU16(output, packet.sequence);
	AppendU16(output, packet.frame_number);
	AppendU32(output, packet.timestamp);
	if (!extension_bytes.empty()) {
		AppendU16(output, static_cast<uint16_t>(extension_words - 1));
		output.insert(output.end(), extension_bytes.begin(), extension_bytes.end());
		output.resize(header_size, 0);
	}
	output.insert(output.end(), packet.payload.begin(), packet.payload.end());
	return output;
}

std::optional<DataTrackPacket> DeserializeDataTrackPacket(const uint8_t* data, std::size_t size) {
	if (data == nullptr || size < kBaseHeaderSize) {
		return std::nullopt;
	}
	const uint8_t initial = data[0];
	if ((initial >> kVersionShift) > kSupportedVersion) {
		return std::nullopt;
	}
	DataTrackPacket packet;
	packet.marker = static_cast<DataTrackFrameMarker>((initial >> kMarkerShift) & 0x03U);
	packet.track_handle = ReadU16(data + 2);
	packet.sequence = ReadU16(data + 4);
	packet.frame_number = ReadU16(data + 6);
	packet.timestamp = ReadU32(data + 8);
	if (packet.track_handle == 0) {
		return std::nullopt;
	}
	std::size_t offset = kBaseHeaderSize;
	if ((initial & kExtensionFlag) != 0) {
		if (size - offset < 2) {
			return std::nullopt;
		}
		const std::size_t extension_size =
		    (static_cast<std::size_t>(ReadU16(data + offset)) + 1) * 4 - 2;
		offset += 2;
		if (extension_size > size - offset) {
			return std::nullopt;
		}
		const std::size_t extension_end = offset + extension_size;
		while (offset + 2 <= extension_end) {
			const uint8_t tag = data[offset++];
			const std::size_t length = data[offset++];
			if (tag == 0) {
				break;
			}
			if (length > extension_end - offset) {
				return std::nullopt;
			}
			if (tag == kE2eeExtensionTag) {
				if (length != 13) {
					return std::nullopt;
				}
				packet.extensions.key_index = data[offset];
				packet.extensions.iv.assign(data + offset + 1, data + offset + length);
			} else if (tag == kUserTimestampExtensionTag) {
				if (length != 8) {
					return std::nullopt;
				}
				packet.extensions.user_timestamp = ReadU64(data + offset);
			}
			offset += length;
		}
		offset = extension_end;
	}
	packet.payload.assign(data + offset, data + size);
	return packet;
}

DataTrackPacketizer::DataTrackPacketizer(uint16_t track_handle, std::size_t mtu)
    : track_handle_(track_handle), mtu_(mtu), timestamp_base_(std::random_device{}()),
      epoch_(std::chrono::steady_clock::now()) {}

std::optional<std::vector<std::vector<uint8_t>>>
DataTrackPacketizer::Packetize(const DataTrackFrame& frame,
                               const std::optional<DataTrackPacketExtensions>& extensions) {
	DataTrackPacket prototype;
	prototype.track_handle = track_handle_;
	prototype.extensions = extensions.value_or(DataTrackPacketExtensions{});
	auto serialized_header = SerializeDataTrackPacket(prototype);
	if (!serialized_header || serialized_header->size() >= mtu_) {
		return std::nullopt;
	}
	const std::size_t header_size = serialized_header->size();
	const std::size_t maximum_payload = mtu_ - header_size;
	const std::size_t packet_count =
	    std::max<std::size_t>(1, (frame.payload.size() + maximum_payload - 1) / maximum_payload);
	if (packet_count > kMaximumPacketsPerFrame) {
		return std::nullopt;
	}
	const uint16_t current_frame = frame_number_++;
	const auto elapsed = std::chrono::steady_clock::now() - epoch_;
	const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() *
	                   90'000 / 1'000'000'000;
	const uint32_t timestamp = timestamp_base_ + static_cast<uint32_t>(ticks);
	std::vector<std::vector<uint8_t>> packets;
	packets.reserve(packet_count);
	for (std::size_t index = 0; index < packet_count; ++index) {
		DataTrackPacket packet;
		packet.marker =
		    packet_count == 1
		        ? DataTrackFrameMarker::Single
		        : (index == 0 ? DataTrackFrameMarker::Start
		                      : (index + 1 == packet_count ? DataTrackFrameMarker::Final
		                                                   : DataTrackFrameMarker::Intermediate));
		packet.track_handle = track_handle_;
		packet.sequence = sequence_++;
		packet.frame_number = current_frame;
		packet.timestamp = timestamp;
		packet.extensions = prototype.extensions;
		const auto begin = std::min(index * maximum_payload, frame.payload.size());
		const auto end = std::min(begin + maximum_payload, frame.payload.size());
		packet.payload.assign(frame.payload.begin() + static_cast<std::ptrdiff_t>(begin),
		                      frame.payload.begin() + static_cast<std::ptrdiff_t>(end));
		auto serialized = SerializeDataTrackPacket(packet);
		if (!serialized) {
			return std::nullopt;
		}
		packets.push_back(std::move(*serialized));
	}
	return packets;
}

DataTrackDepacketizer::DataTrackDepacketizer(std::size_t max_partial_frames)
    : max_partial_frames_(std::max<std::size_t>(1, max_partial_frames)) {}

std::optional<DataTrackAssembledFrame> DataTrackDepacketizer::Push(DataTrackPacket packet) {
	if (packet.marker == DataTrackFrameMarker::Single) {
		DataTrackAssembledFrame frame;
		frame.frame = {std::move(packet.payload), packet.extensions.user_timestamp};
		frame.extensions = std::move(packet.extensions);
		return frame;
	}
	auto found = partials_.find(packet.frame_number);
	if (found == partials_.end()) {
		EnsureCapacity(packet.frame_number);
		found = partials_.try_emplace(packet.frame_number).first;
		insertion_order_.push_back(packet.frame_number);
	}
	if (found->second.payloads.size() >= kMaximumPacketsPerFrame &&
	    !found->second.payloads.contains(packet.sequence)) {
		return std::nullopt;
	}
	if (packet.marker == DataTrackFrameMarker::Start) {
		found->second.start_sequence = packet.sequence;
		found->second.extensions = packet.extensions;
	}
	if (packet.marker == DataTrackFrameMarker::Final) {
		found->second.end_sequence = packet.sequence;
	}
	if (!found->second.start_sequence &&
	    (packet.extensions.key_index || packet.extensions.user_timestamp)) {
		found->second.extensions = packet.extensions;
	}
	found->second.payloads[packet.sequence] = std::move(packet.payload);
	return Finish(packet.frame_number);
}

void DataTrackDepacketizer::SetMaximumPartialFrames(std::size_t maximum) {
	max_partial_frames_ = std::max<std::size_t>(1, maximum);
	while (partials_.size() > max_partial_frames_ && !insertion_order_.empty()) {
		partials_.erase(insertion_order_.front());
		insertion_order_.erase(insertion_order_.begin());
	}
}

void DataTrackDepacketizer::Reset() {
	partials_.clear();
	insertion_order_.clear();
}

std::optional<DataTrackAssembledFrame> DataTrackDepacketizer::Finish(uint16_t frame_number) {
	auto found = partials_.find(frame_number);
	if (found == partials_.end() || !found->second.start_sequence || !found->second.end_sequence) {
		return std::nullopt;
	}
	DataTrackAssembledFrame assembled;
	assembled.frame.user_timestamp = found->second.extensions.user_timestamp;
	assembled.extensions = found->second.extensions;
	uint16_t sequence = *found->second.start_sequence;
	std::size_t packet_count = 0;
	while (true) {
		auto payload = found->second.payloads.find(sequence);
		if (payload == found->second.payloads.end()) {
			return std::nullopt;
		}
		assembled.frame.payload.insert(assembled.frame.payload.end(), payload->second.begin(),
		                               payload->second.end());
		if (sequence == *found->second.end_sequence) {
			break;
		}
		if (++packet_count >= kMaximumPacketsPerFrame) {
			return std::nullopt;
		}
		++sequence;
	}
	partials_.erase(found);
	insertion_order_.erase(
	    std::remove(insertion_order_.begin(), insertion_order_.end(), frame_number),
	    insertion_order_.end());
	return assembled;
}

void DataTrackDepacketizer::EnsureCapacity(uint16_t incoming_frame_number) {
	partials_.erase(incoming_frame_number);
	insertion_order_.erase(
	    std::remove(insertion_order_.begin(), insertion_order_.end(), incoming_frame_number),
	    insertion_order_.end());
	while (partials_.size() >= max_partial_frames_ && !insertion_order_.empty()) {
		partials_.erase(insertion_order_.front());
		insertion_order_.erase(insertion_order_.begin());
	}
}

} // namespace detail
} // namespace core
} // namespace livekit
