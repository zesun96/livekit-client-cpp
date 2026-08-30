#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_FRAME_H_
#define _LKC_CORE_TRACK_VIDEO_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace livekit {
namespace core {

enum class VideoRotation : std::uint16_t {
	Rotation0 = 0,
	Rotation90 = 90,
	Rotation180 = 180,
	Rotation270 = 270,
};

// Pixel byte order is the order in memory. Planar formats store planes in the order named below.
enum class VideoBufferType : std::uint8_t {
	RGBA = 0,
	ABGR,
	ARGB,
	BGRA,
	RGB24,
	I420,
	I420A,
	I422,
	I444,
	I010,
	NV12,
};

// Describes one plane within VideoFrame::data. Stride and size are expressed in bytes.
struct VideoPlaneInfo {
	std::size_t offset = 0;
	std::size_t size = 0;
	std::uint32_t stride = 0;
};

// Optional application metadata transported alongside an encoded video frame. The metadata is
// carried by LiveKit's packet-trailer protocol and is removed before the receiver decodes the
// frame. user_data is limited to 232 bytes so every combination of fields fits in one trailer.
struct VideoFrameMetadata {
	std::optional<std::uint64_t> user_timestamp_us;
	std::optional<std::uint32_t> frame_id;
	std::optional<std::vector<std::uint8_t>> user_data;
};

inline constexpr std::size_t kMaxVideoFrameMetadataUserDataSize = 232;

// An owned video frame. An empty planes vector selects the canonical tightly packed layout for the
// configured format. Existing callers that only populate the original fields continue to submit
// tightly packed I420.
struct VideoFrame {
	std::vector<uint8_t> data;
	uint32_t width = 0;
	uint32_t height = 0;
	int64_t timestamp_us = 0;
	VideoRotation rotation = VideoRotation::Rotation0;
	VideoBufferType format = VideoBufferType::I420;
	std::vector<VideoPlaneInfo> planes;
	std::optional<VideoFrameMetadata> metadata;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_FRAME_H_
