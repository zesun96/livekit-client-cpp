/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef LKC_CORE_MEDIA_DEVICE_H
#define LKC_CORE_MEDIA_DEVICE_H

#include <cstdint>
#include <string>
#include <vector>

namespace livekit::core {

enum class MediaDeviceKind {
	AudioInput,
	AudioOutput,
	VideoInput,
};

struct MediaDeviceInfo {
	std::string id;
	std::string label;
	MediaDeviceKind kind = MediaDeviceKind::AudioInput;
	bool is_default = false;
};

struct AudioPlaybackStats {
	std::uint64_t queued_frames = 0;
	std::uint64_t played_frames = 0;
	std::uint64_t dropped_frames = 0;
	std::uint64_t underrun_frames = 0;
	std::uint32_t buffered_duration_ms = 0;
	std::uint32_t device_latency_ms = 0;
	std::uint32_t estimated_delay_ms = 0;
};

// Returns the active media devices visible to the current process. An empty list is valid when
// the host has no devices or device access is unavailable. This function does not request capture
// permission and does not initialize or change the device used by an existing room.
std::vector<MediaDeviceInfo> EnumerateMediaDevices();

} // namespace livekit::core

#endif // LKC_CORE_MEDIA_DEVICE_H
