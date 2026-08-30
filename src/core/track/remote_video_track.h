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
#include <memory>
#include <mutex>
#include <vector>

namespace livekit::core::detail {
class FrameMetadataStore;
}

namespace livekit {
namespace core {

class RemoteVideoTrack : public RemoteTrack,
                         private webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
	using FrameCallback = std::function<void(const VideoFrame&)>;

	RemoteVideoTrack(std::string sid, std::string name, std::unique_ptr<VideoTrack> video_track,
	                 FrameCallback callback,
	                 std::shared_ptr<detail::FrameMetadataStore> metadata_store = {});
	~RemoteVideoTrack() override;
	std::shared_ptr<VideoStream> CreateVideoStream(MediaStreamOptions options = {}) override;
	std::shared_ptr<detail::FrameMetadataStore> GetFrameMetadataStore() const;

private:
	void OnFrame(const webrtc::VideoFrame& frame) override;
	void CloseStreams();

	FrameCallback callback_;
	std::shared_ptr<detail::FrameMetadataStore> metadata_store_;
	std::mutex streams_mutex_;
	std::vector<std::weak_ptr<VideoStream>> streams_;
};

} // namespace core
} // namespace livekit

#endif //
