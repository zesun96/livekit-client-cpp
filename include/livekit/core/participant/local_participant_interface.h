/**
 *
 * Copyright (c) 2024 sunze
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

#ifndef _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
#define _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_

#include "../track/audio_source_interface.h"
#include "../track/local_track_interface.h"
#include "../track/track_interface.h"

#include <map>
#include <memory>
#include <string>

namespace livekit {

namespace core {

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
};

class LocalParticipantInterface {
public:
	virtual ~LocalParticipantInterface() = default;

	virtual LocalTrackInterface* CreateLocalAudioTreack(std::string label,
	                                                    AudioSourceInterface* source) = 0;

	virtual bool PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
