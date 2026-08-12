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

#ifndef _LKC_CORE_TRACK_REMOTE_VIDEO_TRACK_H_
#define _LKC_CORE_TRACK_REMOTE_VIDEO_TRACK_H_

#include "remote_track.h"
#include "video_track.h"

#include "livekit/core/track/video_frame.h"

#include <functional>

namespace livekit {
namespace core {

class RemoteVideoTrack : public RemoteTrack,
                         private webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
	using FrameCallback = std::function<void(const VideoFrame&)>;

	RemoteVideoTrack(std::string sid, std::string name, std::unique_ptr<VideoTrack> video_track,
	                 FrameCallback callback);
	~RemoteVideoTrack() override;

private:
	void OnFrame(const webrtc::VideoFrame& frame) override;

	FrameCallback callback_;
};

} // namespace core
} // namespace livekit

#endif //
