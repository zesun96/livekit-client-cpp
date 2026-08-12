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

#ifndef _LKC_CORE_TRACK_VIDEO_TRACK_H_
#define _LKC_CORE_TRACK_VIDEO_TRACK_H_

#include "media_stream_track.h"

#include "api/video/video_sink_interface.h"

#include <memory>
#include <mutex>
#include <vector>

namespace livekit {
namespace core {

class VideoTrack : public MediaStreamTrack {
public:
	explicit VideoTrack(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track);
	~VideoTrack() override;

	void AddSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);
	void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);

private:
	webrtc::VideoTrackInterface* track() const;

	std::mutex sinks_mutex_;
	std::vector<webrtc::VideoSinkInterface<webrtc::VideoFrame>*> sinks_;
};

} // namespace core
} // namespace livekit

#endif //
