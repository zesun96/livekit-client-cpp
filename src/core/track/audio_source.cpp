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
#include "../detail/repeating_task_utils.h"

#include "api/units/time_delta.h"
#include "api/units/timestamp.h"

#include <limits>
#include <stdexcept>

namespace livekit {
namespace core {

inline webrtc::AudioOptions to_webrtc_audio_options(const AudioSourceOptions& options) {
	webrtc::AudioOptions rtc_options{};
	rtc_options.echo_cancellation = options.echo_cancellation;
	rtc_options.noise_suppression = options.noise_suppression;
	rtc_options.auto_gain_control = options.auto_gain_control;
	return rtc_options;
}

AudioSource::AudioSource(AudioSourceOptions options, uint32_t sample_rate, uint32_t num_channels,
                         uint32_t queue_size_ms, webrtc::TaskQueueFactory* task_queue_factory)
    : options_(options), sample_rate_(sample_rate), num_channels_(num_channels),
      source_(webrtc::make_ref_counted<InternalSource>(to_webrtc_audio_options(options),
                                                       sample_rate, num_channels, queue_size_ms,
                                                       task_queue_factory)) {}

bool AudioSource::CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
                               uint32_t samples_per_channel) {
	// process audio data
	return source_->capture_frame(audio_data, sample_rate, num_channels, samples_per_channel);
}

void AudioSource::SetAudioOptions(const AudioSourceOptions& options) const {
	source_->set_options(to_webrtc_audio_options(options));
}

void AudioSource::ClearBuffer() const { source_->clear_buffer(); }

std::chrono::milliseconds AudioSource::QueuedDuration() const noexcept {
	return source_->queued_duration();
}

bool AudioSource::ClearQueue() const noexcept {
	source_->clear_buffer();
	return true;
}

bool AudioSource::WaitForPlayout(std::chrono::milliseconds timeout) const noexcept {
	return source_->wait_for_playout(timeout);
}

AudioSource* AudioSource::Create(AudioSourceOptions options, uint32_t sample_rate,
                                 uint32_t num_channels, uint32_t queue_size_ms) {
	if (sample_rate == 0 || sample_rate > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
	    sample_rate % 100 != 0) {
		throw std::invalid_argument("sample_rate must represent an integral 10 ms frame");
	}
	if (num_channels == 0 ||
	    num_channels > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
		throw std::invalid_argument("num_channels must be positive");
	}
	if (queue_size_ms > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
	    queue_size_ms % 10 != 0) {
		throw std::invalid_argument("queue_size_ms must be a multiple of 10");
	}
	return new AudioSource(options, sample_rate, num_channels, queue_size_ms,
	                       GetGlobalTaskQueueFactory());
}

AudioSourceInterface* CreateAudioSource(AudioSourceOptions options, uint32_t sample_rate,
                                        uint32_t num_channels, uint32_t queue_size_ms) {
	return AudioSource::Create(options, sample_rate, num_channels, queue_size_ms);
}

AudioSource::InternalSource::InternalSource(const webrtc::AudioOptions& options,
                                            uint32_t sample_rate, uint32_t num_channels,
                                            uint32_t queue_size_ms, // must be a multiple of 10ms
                                            webrtc::TaskQueueFactory* task_queue_factory)
    : sample_rate_(sample_rate), num_channels_(num_channels) {
	if (!queue_size_ms)
		return; // no audio queue

	// start sending silence when there is nothing on the queue for 10 frames
	// (100ms)
	const int silence_frames_threshold = 10;
	missed_frames_ = silence_frames_threshold;

	const std::size_t samples_per_channel_10_ms = sample_rate / 100;
	if (samples_per_channel_10_ms > std::numeric_limits<std::size_t>::max() / num_channels) {
		throw std::invalid_argument("audio frame dimensions are too large");
	}
	const std::size_t samples_10_ms = samples_per_channel_10_ms * num_channels;
	const std::size_t queue_frames = queue_size_ms / 10;
	if (queue_frames > std::numeric_limits<std::size_t>::max() / samples_10_ms) {
		throw std::invalid_argument("audio queue is too large");
	}

	silence_buffer_.resize(samples_10_ms);
	queue_size_samples_ = queue_frames * samples_10_ms;
	buffer_.reserve(queue_size_samples_);

	audio_queue_ = std::move(task_queue_factory->CreateTaskQueue(
	    "AudioSourceCapture", webrtc::TaskQueueFactory::Priority::NORMAL));

	audio_task_ = webrtc::RepeatingTaskHandle::Start(
	    audio_queue_.get(),
	    [this, samples_10_ms, samples_per_channel_10_ms]() {
		    webrtc::MutexLock lock(&mutex_);

		    if (buffer_.size() >= samples_10_ms) {
			    for (auto sink : sinks_)
				    sink->OnData(buffer_.data(), sizeof(int16_t) * 8,
				                 static_cast<int>(sample_rate_), num_channels_,
				                 samples_per_channel_10_ms);

			    buffer_.erase(buffer_.begin(), buffer_.begin() + samples_10_ms);
			    if (buffer_.empty()) {
				    queue_empty_.Set();
			    }
		    } else {
			    missed_frames_++;
			    if (missed_frames_ >= silence_frames_threshold) {
				    for (auto sink : sinks_)
					    sink->OnData(silence_buffer_.data(), sizeof(int16_t) * 8,
					                 static_cast<int>(sample_rate_), num_channels_,
					                 samples_per_channel_10_ms);
			    }
		    }

		    return webrtc::TimeDelta::Millis(10);
	    },
	    webrtc::TaskQueueBase::DelayPrecision::kHigh);
}

AudioSource::InternalSource::~InternalSource() {
	// RepeatingTaskHandle is sequence-bound after its first invocation. Stop it on the audio
	// queue and wait before destroying the queue or any state captured by the repeating task.
	detail::StopRepeatingTaskOnQueue(audio_queue_.get(), audio_task_);
}

bool AudioSource::InternalSource::capture_frame(void* data, uint32_t sample_rate,
                                                uint32_t number_of_channels,
                                                size_t number_of_frames) {
	if (data == nullptr || sample_rate == 0 || number_of_channels == 0 || number_of_frames == 0) {
		return false;
	}
	if (sample_rate != sample_rate_ || number_of_channels != num_channels_) {
		return false;
	}
	const std::size_t samples_per_channel_10_ms = sample_rate_ / 100;
	if (number_of_frames % samples_per_channel_10_ms != 0 ||
	    (!queue_size_samples_ && number_of_frames != samples_per_channel_10_ms)) {
		return false;
	}
	webrtc::MutexLock lock(&mutex_);
	if (number_of_frames > std::numeric_limits<std::size_t>::max() / number_of_channels) {
		return false;
	}
	const std::size_t total_samples = number_of_frames * number_of_channels;

	if (queue_size_samples_) {
		if (buffer_.size() > queue_size_samples_ ||
		    total_samples > queue_size_samples_ - buffer_.size()) {
			return false;
		}

		int16_t* pcm_data = static_cast<int16_t*>(data);
		buffer_.insert(buffer_.end(), pcm_data, pcm_data + total_samples);
		queue_empty_.Reset();
		missed_frames_ = 0;
	} else {
		// capture directly when the queue buffer is 0 (frame size must be 10ms)
		for (auto sink : sinks_)
			sink->OnData(data, sizeof(int16_t) * 8, sample_rate, number_of_channels,
			             number_of_frames);
	}

	return true;
}

void AudioSource::InternalSource::clear_buffer() noexcept {
	webrtc::MutexLock lock(&mutex_);
	buffer_.clear();
	queue_empty_.Set();
}

std::chrono::milliseconds AudioSource::InternalSource::queued_duration() const noexcept {
	webrtc::MutexLock lock(&mutex_);
	const std::size_t queued_frames = buffer_.size() / num_channels_;
	const std::size_t whole_seconds = queued_frames / sample_rate_;
	const std::size_t remaining_frames = queued_frames % sample_rate_;
	const auto duration_ms = whole_seconds * 1000 + remaining_frames * 1000 / sample_rate_;
	return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(duration_ms));
}

bool AudioSource::InternalSource::wait_for_playout(std::chrono::milliseconds timeout) noexcept {
	if (timeout.count() < 0) {
		return false;
	}
	return queue_empty_.Wait(webrtc::TimeDelta::Millis(timeout.count()));
}

webrtc::MediaSourceInterface::SourceState AudioSource::InternalSource::state() const {
	return webrtc::MediaSourceInterface::SourceState::kLive;
}

bool AudioSource::InternalSource::remote() const { return false; }

const webrtc::AudioOptions AudioSource::InternalSource::options() const {
	webrtc::MutexLock lock(&mutex_);
	return options_;
}

void AudioSource::InternalSource::set_options(const webrtc::AudioOptions& options) {
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

webrtc::scoped_refptr<AudioSource::InternalSource> AudioSource::Get() const { return source_; }

std::chrono::milliseconds
GetAudioSourceQueuedDuration(const AudioSourceInterface* source) noexcept {
	const auto* concrete = dynamic_cast<const AudioSource*>(source);
	return concrete != nullptr ? concrete->QueuedDuration() : std::chrono::milliseconds::zero();
}

bool ClearAudioSourceQueue(AudioSourceInterface* source) noexcept {
	auto* concrete = dynamic_cast<AudioSource*>(source);
	return concrete != nullptr && concrete->ClearQueue();
}

bool WaitForAudioSourcePlayout(AudioSourceInterface* source,
                               std::chrono::milliseconds timeout) noexcept {
	auto* concrete = dynamic_cast<AudioSource*>(source);
	return concrete != nullptr && concrete->WaitForPlayout(timeout);
}

} // namespace core
} // namespace livekit
