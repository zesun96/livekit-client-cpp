/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifndef _LKC_CORE_TRACK_ENCODED_VIDEO_FRAME_H_
#define _LKC_CORE_TRACK_ENCODED_VIDEO_FRAME_H_

#include <cstdint>
#include <vector>

namespace livekit {
namespace core {

enum class EncodedVideoCodec {
	Unknown,
	VP8,
	VP9,
	H264,
	H265,
	AV1,
};

// An owned encoded frame received from a remote video track. H264 and H265 payloads use Annex-B
// start codes. The timestamp is the libwebrtc render timestamp expressed in microseconds.
struct EncodedVideoFrame {
	std::vector<std::uint8_t> data;
	EncodedVideoCodec codec = EncodedVideoCodec::Unknown;
	bool key_frame = false;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::int64_t timestamp_us = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_ENCODED_VIDEO_FRAME_H_
