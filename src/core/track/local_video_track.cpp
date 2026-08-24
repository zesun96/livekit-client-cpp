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

#include "local_video_track.h"

namespace livekit {
namespace core {

LocalVideoTrack::LocalVideoTrack(std::string name, std::unique_ptr<VideoTrack> video_track,
                                 VideoSourceInterface* source)
    : LocalTrack("TR_unknown", std::move(name), TrackKind::Video, std::move(video_track)),
      source_(source) {}

void LocalVideoTrack::SetEnabled(bool enabled) {
	LocalTrack::SetEnabled(enabled);
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	for (auto& entry : additional_codecs_) {
		auto& state = entry.second;
		if (state.media_track != nullptr) {
			state.media_track->set_enabled(enabled);
		}
	}
}

bool LocalVideoTrack::HasAdditionalCodec(VideoCodec codec) const {
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	return additional_codecs_.count(codec) != 0;
}

bool LocalVideoTrack::AddAdditionalCodec(
    VideoCodec codec, std::unique_ptr<VideoTrack> media_track,
    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver,
    std::vector<webrtc::RtpEncodingParameters> encodings) {
	if (media_track == nullptr || transceiver == nullptr) {
		return false;
	}
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	return additional_codecs_
	    .emplace(codec, AdditionalCodecState{std::move(media_track), std::move(transceiver),
	                                         std::move(encodings)})
	    .second;
}

std::vector<LocalVideoTrack::AdditionalCodecSender> LocalVideoTrack::AdditionalCodecs() const {
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	std::vector<AdditionalCodecSender> result;
	result.reserve(additional_codecs_.size());
	for (const auto& [codec, state] : additional_codecs_) {
		result.push_back({codec, state.transceiver, state.encodings});
	}
	return result;
}

bool LocalVideoTrack::UpdateAdditionalCodecEncodings(
    VideoCodec codec, std::vector<webrtc::RtpEncodingParameters> encodings) {
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	const auto state = additional_codecs_.find(codec);
	if (state == additional_codecs_.end()) {
		return false;
	}
	state->second.encodings = std::move(encodings);
	return true;
}

void LocalVideoTrack::ClearAdditionalCodecs() {
	std::lock_guard<std::mutex> guard(additional_codecs_mutex_);
	additional_codecs_.clear();
}

} // namespace core
} // namespace livekit
