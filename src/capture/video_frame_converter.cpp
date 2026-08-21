/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "video_frame_converter.h"

#include "libyuv/convert.h"

#include <limits>

namespace livekit::capture {

bool ConvertBgraToI420(const std::uint8_t* data, std::uint32_t source_width,
                       std::uint32_t source_height, std::uint32_t row_stride_bytes,
                       std::int64_t timestamp_us, CapturedVideoFrame& destination) {
	const std::uint32_t width = source_width & ~1U;
	const std::uint32_t height = source_height & ~1U;
	if (data == nullptr || width == 0 || height == 0 ||
	    source_width > std::numeric_limits<std::uint32_t>::max() / 4U ||
	    width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
	    row_stride_bytes < source_width * 4U ||
	    row_stride_bytes > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		return false;
	}
	const std::size_t y_size = static_cast<std::size_t>(width) * height;
	const std::size_t chroma_size = y_size / 4;
	destination.i420.resize(y_size + chroma_size * 2);
	auto* y = destination.i420.data();
	auto* u = y + y_size;
	auto* v = u + chroma_size;
	if (libyuv::ARGBToI420(data, static_cast<int>(row_stride_bytes), y, static_cast<int>(width), u,
	                       static_cast<int>(width / 2), v, static_cast<int>(width / 2),
	                       static_cast<int>(width), static_cast<int>(height)) != 0) {
		return false;
	}
	destination.width = width;
	destination.height = height;
	destination.timestamp_us = timestamp_us;
	return true;
}

} // namespace livekit::capture
