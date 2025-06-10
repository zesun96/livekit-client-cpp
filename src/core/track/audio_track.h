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

#ifndef _LKC_CORE_TRACK_AUDIO_TRACK_H_
#define _LKC_CORE_TRACK_AUDIO_TRACK_H_

#include "media_stream_track.h"

#include "api/audio/audio_frame.h"
#include "common_audio/resampler/include/push_resampler.h"
#include "rtc_base/synchronization/mutex.h"

namespace livekit {
namespace core {

class AudioSinkWrapper {
public:
	virtual ~AudioSinkWrapper() = default;
	virtual void on_data(const void* audio_data, int bits_per_sample, int sample_rate,
	                     size_t number_of_channels, size_t number_of_frames) = 0;
};

class AudioSink : public webrtc::AudioTrackSinkInterface {
public:
	explicit AudioSink(std::unique_ptr<AudioSinkWrapper> observer, int sample_rate,
	                   int num_channels);

	void OnData(const void* audio_data, int bits_per_sample, int sample_rate,
	            size_t number_of_channels, size_t number_of_frames) override;

private:
	std::unique_ptr<AudioSinkWrapper> observer_;

	int sample_rate_;
	int num_channels_;

	webrtc::AudioFrame frame_;
	webrtc::PushResampler<int16_t> resampler_;
};

class AudioTrack : public MediaStreamTrack {
public:
	AudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track);

	virtual ~AudioTrack();

	void add_sink(const std::shared_ptr<AudioSink>& sink) const;
	void remove_sink(const std::shared_ptr<AudioSink>& sink) const;

private:
	webrtc::AudioTrackInterface* track() const {
		return static_cast<webrtc::AudioTrackInterface*>(track_.get());
	}

	mutable webrtc::Mutex mutex_;

	// Same for VideoTrack:
	// Keep a strong reference to the added sinks, so we don't need to
	// manage the lifetime safety on the Rust side
	mutable std::vector<std::shared_ptr<AudioSink>> sinks_;
};

} // namespace core
} // namespace livekit

#endif //
