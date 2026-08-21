#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_FRAME_H_
#define _LKC_CORE_TRACK_VIDEO_FRAME_H_

#include <cstdint>
#include <vector>

namespace livekit {
namespace core {

enum class VideoRotation : std::uint16_t {
	Rotation0 = 0,
	Rotation90 = 90,
	Rotation180 = 180,
	Rotation270 = 270,
};

// A tightly packed I420 frame: Y plane followed by U and V planes.
struct VideoFrame {
	std::vector<uint8_t> data;
	uint32_t width = 0;
	uint32_t height = 0;
	int64_t timestamp_us = 0;
	VideoRotation rotation = VideoRotation::Rotation0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_FRAME_H_
