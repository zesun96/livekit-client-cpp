/**
 *
 * Copyright (c) 2025 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#pragma once

#ifndef _LKC_CORE_OPTION_MEDIA_OPTION_H_
#define _LKC_CORE_OPTION_MEDIA_OPTION_H_

#include <cstdint>
#include <optional>
#include <string>

namespace livekit {
namespace core {

enum class TrackKind {
	Unknown = 0,
	Audio = 1,
	Video = 2,
};

enum class TrackSource {
	Unknown = 0,
	Camera = 1,
	Microphone = 2,
	ScreenShare = 3,
	ScreenShareAudio = 4,
};

enum class TrackStreamState {
	Unknown = 0,
	Active = 1,
	Paused = 2,
};

enum class TrackSubscriptionStatus {
	Unsubscribed = 0,
	Desired = 1,
	Subscribed = 2,
};

// Values intentionally match livekit.protocol.VideoQuality.
enum class VideoQuality {
	Low = 0,
	Medium = 1,
	High = 2,
	Off = 3,
};

enum class ConnectionQuality {
	Unknown = 0,
	Poor,
	Good,
	Excellent,
	Lost,
};

struct TrackDimensions {
	uint32_t width = 0;
	uint32_t height = 0;
};

struct RemoteTrackSettings {
	bool enabled = true;
	std::optional<VideoQuality> video_quality;
	std::optional<TrackDimensions> video_dimensions;
	uint32_t video_fps = 0;
	uint32_t priority = 0;
};

enum class VideoCodec {
	VP8,
	H264,
	VP9,
	AV1,
};

struct VideoEncoding {
	uint64_t max_bitrate = 0;
	float max_framerate = 0.0f;
};

struct VideoPreset {
	VideoEncoding encoding = {0, 0.0f};
	uint32_t width = 0;
	uint32_t height = 0;
};

struct AudioEncoding {
	uint64_t max_bitrate = 0;
};

struct AudioPreset {
	AudioEncoding encoding = {0};
};

struct TrackPublishOptions {
	VideoEncoding video_encoding;
	AudioEncoding audio_encoding;
	VideoCodec video_codec = VideoCodec::VP8;
	bool dtx = true;
	bool red = true;
	bool simulcast = true;
	TrackSource source = TrackSource::Unknown;
	std::string stream;
	std::string scalability_mode = "L3T3_KEY";
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_OPTION_MEDIA_OPTION_H_
