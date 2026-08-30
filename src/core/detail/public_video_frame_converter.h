/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "livekit/core/track/video_frame.h"

#include <vector>

namespace livekit::core::detail {

// Resolves either the canonical tightly packed layout or the caller-provided plane layout.
// Returns false when dimensions, strides, sizes, offsets, or the backing buffer are invalid.
bool ResolveVideoFramePlanes(const VideoFrame& frame, std::vector<VideoPlaneInfo>& planes);

// Converts any supported public VideoBufferType into tightly packed 8-bit I420.
bool ConvertVideoFrameToI420(const VideoFrame& frame, std::vector<std::uint8_t>& i420);

} // namespace livekit::core::detail
