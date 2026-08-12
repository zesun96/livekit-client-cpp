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

#include "video_track.h"

#include <algorithm>

namespace livekit {
namespace core {

VideoTrack::VideoTrack(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track)
    : MediaStreamTrack(std::move(track)) {}

VideoTrack::~VideoTrack() {
	std::lock_guard<std::mutex> guard(sinks_mutex_);
	for (auto* sink : sinks_) {
		track()->RemoveSink(sink);
	}
}

void VideoTrack::AddSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
	if (sink == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> guard(sinks_mutex_);
	webrtc::VideoSinkWants wants;
	track()->AddOrUpdateSink(sink, wants);
	sinks_.push_back(sink);
}

void VideoTrack::RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
	std::lock_guard<std::mutex> guard(sinks_mutex_);
	track()->RemoveSink(sink);
	sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

webrtc::VideoTrackInterface* VideoTrack::track() const {
	return static_cast<webrtc::VideoTrackInterface*>(track_.get());
}

} // namespace core
} // namespace livekit
