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

#ifndef _LKC_CORE_TRACK_AUDIO_SOURCE_H_
#define _LKC_CORE_TRACK_AUDIO_SOURCE_H_

#include "livekit/core/track/audio_source_interface.h"

#include "api/audio/audio_frame.h"
#include "api/audio_options.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "pc/local_audio_source.h"
#include "rtc_base/task_utils/repeating_task.h"

namespace livekit {
namespace core {

class AudioSource : public AudioSourceInterface {
public:
	class InternalSource : public webrtc::LocalAudioSource {
	public:
		InternalSource(const cricket::AudioOptions& options, int sample_rate, int num_channels,
		               int buffer_size_ms, webrtc::TaskQueueFactory* task_queue_factory);

		~InternalSource() override;

		SourceState state() const override;
		bool remote() const override;

		const cricket::AudioOptions options() const override;

		void AddSink(webrtc::AudioTrackSinkInterface* sink) override;
		void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override;

		void set_options(const cricket::AudioOptions& options);

		bool capture_frame(void* audio_data, uint32_t sample_rate, uint32_t number_of_channels,
		                   size_t number_of_frames);

		void clear_buffer();

	private:
		mutable webrtc::Mutex mutex_;
		std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> audio_queue_;
		webrtc::RepeatingTaskHandle audio_task_;

		std::vector<webrtc::AudioTrackSinkInterface*> sinks_ RTC_GUARDED_BY(mutex_);
		std::vector<int16_t> buffer_ RTC_GUARDED_BY(mutex_);

		int missed_frames_ RTC_GUARDED_BY(mutex_) = 0;
		int16_t* silence_buffer_ = nullptr;

		int sample_rate_;
		int num_channels_;
		int queue_size_samples_;
		int notify_threshold_samples_;

		cricket::AudioOptions options_{};
	};

	static AudioSource* Create(AudioSourceOptions options, uint32_t sample_rate,
	                           uint32_t num_channels, uint32_t queue_size_ms);

	virtual ~AudioSource() = default;

	virtual bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                          uint32_t samples_per_channel) override;

	void SetAudioOptions(const AudioSourceOptions& options) const;
	void ClearBuffer() const;

	rtc::scoped_refptr<InternalSource> Get() const;

private:
	AudioSource(AudioSourceOptions options, uint32_t sample_rate, uint32_t num_channels,
	            uint32_t queue_size_samples, webrtc::TaskQueueFactory* task_queue_factory);

private:
	AudioSourceOptions options_;
	uint32_t sample_rate_;
	uint32_t num_channels_;
	uint32_t queue_size_samples_;
	rtc::scoped_refptr<InternalSource> source_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_AUDIO_SOURCE_H_