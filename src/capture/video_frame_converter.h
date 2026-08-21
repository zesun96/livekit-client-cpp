/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

namespace livekit::capture {

struct CapturedVideoFrame {
	std::vector<std::uint8_t> i420;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::int64_t timestamp_us = 0;
	std::uint16_t rotation_degrees = 0;
	bool mirrored = false;
};

bool ConvertBgraToI420(const std::uint8_t* data, std::uint32_t width, std::uint32_t height,
                       std::uint32_t row_stride_bytes, std::int64_t timestamp_us,
                       CapturedVideoFrame& destination);

} // namespace livekit::capture
