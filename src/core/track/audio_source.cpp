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

#include "audio_source.h"

#include "../detail/global_task_queue.h"

#include "api/units/time_delta.h"
#include "api/units/timestamp.h"

#include <stdexcept>

namespace livekit {
namespace core {

inline cricket::AudioOptions to_webrtc_audio_options(const AudioSourceOptions& options) {
	cricket::AudioOptions rtc_options{};
	rtc_options.echo_cancellation = options.echo_cancellation;
	rtc_options.noise_suppression = options.noise_suppression;
	rtc_options.auto_gain_control = options.auto_gain_control;
	return rtc_options;
}

AudioSource::AudioSource(AudioSourceOptions options, uint32_t sample_rate, uint32_t num_channels,
                         uint32_t queue_size_ms, webrtc::TaskQueueFactory* task_queue_factory)
    : options_(options), sample_rate_(sample_rate), num_channels_(num_channels),
      source_(rtc::make_ref_counted<InternalSource>(to_webrtc_audio_options(options), sample_rate,
                                                    num_channels, queue_size_ms,
                                                    task_queue_factory)) {
	queue_size_samples_ = (queue_size_ms * sample_rate / 1000) * num_channels;
}

bool AudioSource::CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
                               uint32_t samples_per_channel) {
	// process audio data
	return source_->capture_frame(audio_data, sample_rate, num_channels, samples_per_channel);
}

void AudioSource::SetAudioOptions(const AudioSourceOptions& options) const {
	source_->set_options(to_webrtc_audio_options(options));
}

void AudioSource::ClearBuffer() const { source_->clear_buffer(); }

AudioSource* AudioSource::Create(AudioSourceOptions options, uint32_t sample_rate,
                                 uint32_t num_channels, uint32_t queue_size_ms) {

	if (queue_size_ms % 10 != 0) {
		throw std::invalid_argument("queue_size_ms must be a multiple of 10");
	}
	return new AudioSource(options, sample_rate, num_channels, queue_size_ms,
	                       GetGlobalTaskQueueFactory());
}

AudioSourceInterface* CreateAudioSource(AudioSourceOptions options, uint32_t sample_rate,
                                        uint32_t num_channels, uint32_t queue_size_ms) {
	return AudioSource::Create(options, sample_rate, num_channels, queue_size_ms);
}

AudioSource::InternalSource::InternalSource(const cricket::AudioOptions& options, int sample_rate,
                                            int num_channels,
                                            int queue_size_ms, // must be a multiple of 10ms
                                            webrtc::TaskQueueFactory* task_queue_factory)
    : sample_rate_(sample_rate), num_channels_(num_channels) {
	if (!queue_size_ms)
		return; // no audio queue

	// start sending silence when there is nothing on the queue for 10 frames
	// (100ms)
	const int silence_frames_threshold = 10;
	missed_frames_ = silence_frames_threshold;

	int samples10ms = sample_rate / 100 * num_channels;

	silence_buffer_ = new int16_t[samples10ms]();
	queue_size_samples_ = queue_size_ms / 10 * samples10ms;
	notify_threshold_samples_ = queue_size_samples_; // TODO: this is currently
	                                                 // using x2 the queue size
	buffer_.reserve(queue_size_samples_ + notify_threshold_samples_);

	audio_queue_ = std::move(task_queue_factory->CreateTaskQueue(
	    "AudioSourceCapture", webrtc::TaskQueueFactory::Priority::NORMAL));

	audio_task_ = webrtc::RepeatingTaskHandle::Start(
	    audio_queue_.get(),
	    [this, samples10ms]() {
		    webrtc::MutexLock lock(&mutex_);

		    if (buffer_.size() >= samples10ms) {
			    for (auto sink : sinks_)
				    sink->OnData(buffer_.data(), sizeof(int16_t) * 8, sample_rate_, num_channels_,
				                 samples10ms / num_channels_);

			    buffer_.erase(buffer_.begin(), buffer_.begin() + samples10ms);
		    } else {
			    missed_frames_++;
			    if (missed_frames_ >= silence_frames_threshold) {
				    for (auto sink : sinks_)
					    sink->OnData(silence_buffer_, sizeof(int16_t) * 8, sample_rate_,
					                 num_channels_, samples10ms / num_channels_);
			    }
		    }

		    return webrtc::TimeDelta::Millis(10);
	    },
	    webrtc::TaskQueueBase::DelayPrecision::kHigh);
}

AudioSource::InternalSource::~InternalSource() {
	audio_task_.Stop();
	delete[] silence_buffer_;
}

bool AudioSource::InternalSource::capture_frame(void* data, uint32_t sample_rate,
                                                uint32_t number_of_channels,
                                                size_t number_of_frames) {
	webrtc::MutexLock lock(&mutex_);
	size_t total_samples = number_of_frames * number_of_channels;

	if (queue_size_samples_) {
		int available = (queue_size_samples_ + notify_threshold_samples_) - buffer_.size();
		if (available < total_samples)
			return false;

		int16_t* pcm_data = static_cast<int16_t*>(data);
		buffer_.assign(pcm_data, pcm_data + total_samples);
	} else {
		// capture directly when the queue buffer is 0 (frame size must be 10ms)
		for (auto sink : sinks_)
			sink->OnData(data, sizeof(int16_t) * 8, sample_rate, number_of_channels,
			             number_of_frames);
	}

	return true;
}

void AudioSource::InternalSource::clear_buffer() {
	webrtc::MutexLock lock(&mutex_);
	buffer_.clear();
}

webrtc::MediaSourceInterface::SourceState AudioSource::InternalSource::state() const {
	return webrtc::MediaSourceInterface::SourceState::kLive;
}

bool AudioSource::InternalSource::remote() const { return false; }

const cricket::AudioOptions AudioSource::InternalSource::options() const {
	webrtc::MutexLock lock(&mutex_);
	return options_;
}

void AudioSource::InternalSource::set_options(const cricket::AudioOptions& options) {
	webrtc::MutexLock lock(&mutex_);
	options_ = options;
}

void AudioSource::InternalSource::AddSink(webrtc::AudioTrackSinkInterface* sink) {
	webrtc::MutexLock lock(&mutex_);
	sinks_.push_back(sink);
}

void AudioSource::InternalSource::RemoveSink(webrtc::AudioTrackSinkInterface* sink) {
	webrtc::MutexLock lock(&mutex_);
	sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

rtc::scoped_refptr<AudioSource::InternalSource> AudioSource::Get() const { return source_; }

} // namespace core
} // namespace livekit