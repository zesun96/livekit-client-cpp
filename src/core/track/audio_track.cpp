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

#include "audio_track.h"

#include "audio/remix_resample.h"

namespace livekit {
namespace core {

AudioSink::AudioSink(std::unique_ptr<AudioSinkWrapper> observer, int sample_rate, int num_channels)
    : observer_(std::move(observer)), sample_rate_(sample_rate), num_channels_(num_channels) {
	frame_.sample_rate_hz_ = sample_rate;
	frame_.num_channels_ = num_channels;
}

void AudioSink::OnData(const void* audio_data, int bits_per_sample, int sample_rate,
                       size_t number_of_channels, size_t number_of_frames) {
	RTC_CHECK_EQ(16, bits_per_sample);

	const int16_t* data = static_cast<const int16_t*>(audio_data);

	if (sample_rate_ != sample_rate || num_channels_ != number_of_channels) {
		// resample/remix before capturing
		webrtc::voe::RemixAndResample(data, number_of_frames, number_of_channels, sample_rate,
		                              &resampler_, &frame_);

		observer_->on_data(frame_.data(), frame_.sample_rate_hz(), frame_.samples_per_channel(),
		                   frame_.num_channels(), number_of_frames);

	} else {

		observer_->on_data(data, bits_per_sample, sample_rate, number_of_channels,
		                   number_of_frames);
	}
}

AudioTrack::AudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track)
    : MediaStreamTrack(std::move(track)) {}

AudioTrack::~AudioTrack() {
	webrtc::MutexLock lock(&mutex_);
	for (auto& sink : sinks_) {
		track()->RemoveSink(sink.get());
	}
}

void AudioTrack::add_sink(const std::shared_ptr<AudioSink>& sink) const {
	webrtc::MutexLock lock(&mutex_);
	track()->AddSink(sink.get());
	sinks_.push_back(sink);
}

void AudioTrack::remove_sink(const std::shared_ptr<AudioSink>& sink) const {
	webrtc::MutexLock lock(&mutex_);
	track()->RemoveSink(sink.get());
	sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

} // namespace core
} // namespace livekit
