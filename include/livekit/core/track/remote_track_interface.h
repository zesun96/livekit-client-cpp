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

#ifndef _LKC_CORE_TRACK_REMOTE_TRACK_INTERFACE_H_
#define _LKC_CORE_TRACK_REMOTE_TRACK_INTERFACE_H_

#include "media_stream.h"
#include "track_interface.h"

namespace livekit {
namespace core {

class RemoteTrackInterface : public virtual TrackInterface {
public:
	virtual ~RemoteTrackInterface() = default;
	// Returns null when the track kind does not match or capacity is zero. Streams are closed when
	// the remote track is unsubscribed or destroyed; existing RoomEvent callbacks remain active.
	virtual std::shared_ptr<AudioStream> CreateAudioStream(MediaStreamOptions options = {}) {
		(void)options;
		return nullptr;
	}
	virtual std::shared_ptr<VideoStream> CreateVideoStream(MediaStreamOptions options = {}) {
		(void)options;
		return nullptr;
	}
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_REMOTE_TRACK_INTERFACE_H_
