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

#ifndef _LKC_CORE_TRACK_MEDIA_STREAM_TRACK_H_
#define _LKC_CORE_TRACK_MEDIA_STREAM_TRACK_H_

#include "api/media_stream_interface.h"

#include <string>

namespace livekit {
namespace core {

enum class TrackState { kLive, kEnded };

class MediaStreamTrack {
protected:
	MediaStreamTrack(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track);

public:
	virtual ~MediaStreamTrack() = default;

	std::string kind() const;
	std::string id() const;

	bool enabled() const;
	bool set_enabled(bool enabled) const;

	TrackState state() const;

	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> rtc_track() const { return track_; }

protected:
	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_MEDIA_STREAM_TRACK_H_