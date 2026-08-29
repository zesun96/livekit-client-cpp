/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "microphone_audio_source.h"

#include "../../capture/audio_gain.h"
#include "../detail/global_task_queue.h"

#include <algorithm>
#include <array>
#include <utility>

namespace livekit::core {
namespace {

constexpr uint32_t kMicrophoneSampleRate = 48000;
constexpr uint32_t kMicrophoneChannels = 1;

} // namespace

MicrophoneAudioSource::MicrophoneAudioSource(MicrophoneCaptureOptions options)
    : AudioSource(options.processing, kMicrophoneSampleRate, kMicrophoneChannels,
                  options.queue_size_ms, GetGlobalTaskQueueFactory()),
      processing_options_(options.processing),
      capture_(std::make_unique<capture::AudioCaptureAdapter>(
          std::move(options.device_id),
          [this](const std::int16_t* samples, std::uint32_t sample_rate, std::uint32_t channels,
                 std::uint32_t frames_per_channel, std::int64_t timestamp_us) {
	          OnAudioFrame(samples, sample_rate, channels, frames_per_channel, timestamp_us);
          })),
      processor_(processing_options_.echo_cancellation, processing_options_.auto_gain_control,
                 processing_options_.noise_suppression) {}

MicrophoneAudioSource::~MicrophoneAudioSource() { Stop(); }

bool MicrophoneAudioSource::CaptureFrame(void* audio_data, uint32_t sample_rate,
                                         uint32_t num_channels, uint32_t samples_per_channel) {
	return AudioSource::CaptureFrame(audio_data, sample_rate, num_channels, samples_per_channel);
}

bool MicrophoneAudioSource::Start() { return capture_ && capture_->Start(); }

void MicrophoneAudioSource::Stop() {
	if (capture_) {
		capture_->Stop();
	}
	std::lock_guard<std::mutex> guard(capture_buffer_mutex_);
	capture_buffer_.clear();
	render_processing_buffer_.clear();
	last_render_sequence_ = 0;
}

bool MicrophoneAudioSource::IsCapturing() const { return capture_ && capture_->IsRunning(); }

std::string MicrophoneAudioSource::DeviceId() const {
	return capture_ ? capture_->DeviceId() : std::string{};
}

bool MicrophoneAudioSource::SwitchDevice(const std::string& device_id) {
	if (device_id.empty() || !capture_) {
		return false;
	}
	const bool switched = capture_->SwitchDevice(device_id);
	std::lock_guard<std::mutex> guard(capture_buffer_mutex_);
	capture_buffer_.clear();
	render_processing_buffer_.clear();
	last_render_sequence_ = 0;
	return switched;
}

void MicrophoneAudioSource::SetMuted(bool muted) { muted_.store(muted); }

bool MicrophoneAudioSource::IsMuted() const { return muted_.load(); }

bool MicrophoneAudioSource::SetVolume(float volume) {
	if (!capture::IsAudioGainValid(volume)) {
		return false;
	}
	volume_.store(volume);
	return true;
}

float MicrophoneAudioSource::Volume() const { return volume_.load(); }

bool MicrophoneAudioSource::SetProcessingOptions(AudioSourceOptions options) {
	if (!processor_.Configure(options.echo_cancellation, options.auto_gain_control,
	                          options.noise_suppression)) {
		return false;
	}
	AudioSource::SetAudioOptions(options);
	std::lock_guard<std::mutex> guard(processing_options_mutex_);
	processing_options_ = options;
	return true;
}

AudioSourceOptions MicrophoneAudioSource::ProcessingOptions() const {
	std::lock_guard<std::mutex> guard(processing_options_mutex_);
	return processing_options_;
}

MicrophoneAudioProcessingStats MicrophoneAudioSource::ProcessingStats() const {
	const auto options = ProcessingOptions();
	const auto aec = processor_.GetStats();
	MicrophoneAudioProcessingStats result;
	result.capture_frames_processed = capture_frames_processed_.load();
	result.render_frames_processed = render_frames_processed_.load();
	result.capture_processing_errors = capture_processing_errors_.load();
	result.render_processing_errors = render_processing_errors_.load();
	result.frames_dropped = frames_dropped_.load();
	result.echo_cancellation_enabled = options.echo_cancellation;
	result.echo_return_loss_available = aec.echo_return_loss_available;
	result.echo_return_loss_db = aec.echo_return_loss_db;
	result.echo_return_loss_enhancement_available =
	    aec.echo_return_loss_enhancement_available;
	result.echo_return_loss_enhancement_db = aec.echo_return_loss_enhancement_db;
	result.residual_echo_likelihood_available = aec.residual_echo_likelihood_available;
	result.residual_echo_likelihood = aec.residual_echo_likelihood;
	result.residual_echo_likelihood_recent_max_available =
	    aec.residual_echo_likelihood_recent_max_available;
	result.residual_echo_likelihood_recent_max = aec.residual_echo_likelihood_recent_max;
	result.delay_median_available = aec.delay_median_available;
	result.delay_median_ms = aec.delay_median_ms;
	result.delay_standard_deviation_available = aec.delay_standard_deviation_available;
	result.delay_standard_deviation_ms = aec.delay_standard_deviation_ms;
	result.delay_available = aec.delay_available;
	result.delay_ms = aec.delay_ms;
	return result;
}

bool MicrophoneAudioSource::BindAudioDevice(webrtc::scoped_refptr<AudioDevice> audio_device) {
	if (audio_device == nullptr) {
		return false;
	}
	std::lock_guard<std::mutex> guard(audio_device_mutex_);
	if (audio_device_ != nullptr && audio_device_ != audio_device) {
		return false;
	}
	audio_device_ = std::move(audio_device);
	return true;
}

void MicrophoneAudioSource::OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
                                         std::uint32_t channels, std::uint32_t frames_per_channel,
                                         std::int64_t) noexcept {
	if (samples == nullptr || sample_rate != kMicrophoneSampleRate ||
	    channels != kMicrophoneChannels || frames_per_channel == 0) {
		return;
	}
	try {
		std::lock_guard<std::mutex> guard(capture_buffer_mutex_);
		capture_buffer_.insert(capture_buffer_.end(), samples, samples + frames_per_channel);
		while (capture_buffer_.size() >= kMicrophoneSampleRate / 100) {
			const auto processing_options = ProcessingOptions();
			std::array<std::int16_t, kMicrophoneSampleRate / 100> processed;
			std::copy_n(capture_buffer_.begin(), processed.size(), processed.begin());
			capture_buffer_.erase(capture_buffer_.begin(),
			                      capture_buffer_.begin() + processed.size());
			std::uint32_t render_sample_rate = 0;
			std::uint32_t render_channels = 0;
			std::uint16_t playout_delay_ms = 0;
			webrtc::scoped_refptr<AudioDevice> audio_device;
			{
				std::lock_guard<std::mutex> audio_device_guard(audio_device_mutex_);
				audio_device = audio_device_;
			}
			if (processing_options.echo_cancellation && audio_device != nullptr &&
			    audio_device->ReadRenderData(last_render_sequence_, render_processing_buffer_,
			                                 render_sample_rate, render_channels)) {
				audio_device->PlayoutDelay(&playout_delay_ms);
				if (processor_.ProcessRender(render_processing_buffer_, render_sample_rate,
				                             render_channels)) {
					render_frames_processed_.fetch_add(1);
				} else {
					render_processing_errors_.fetch_add(1);
				}
			}
			if (!processor_.ProcessCapture(processed, sample_rate, channels, playout_delay_ms)) {
				capture_processing_errors_.fetch_add(1);
				continue;
			}
			capture_frames_processed_.fetch_add(1);
			const float gain = muted_.load() ? 0.0F : volume_.load();
			if (!capture::ApplyAudioGain(processed, processed, gain) ||
			    !AudioSource::CaptureFrame(processed.data(), sample_rate, channels,
			                               processed.size())) {
				frames_dropped_.fetch_add(1);
			}
		}
	} catch (...) {
		frames_dropped_.fetch_add(1);
	}
}

bool SetMicrophoneSourceVolume(MicrophoneAudioSourceInterface* source, float volume) {
	auto* microphone = dynamic_cast<MicrophoneAudioSource*>(source);
	return microphone != nullptr && microphone->SetVolume(volume);
}

float GetMicrophoneSourceVolume(const MicrophoneAudioSourceInterface* source) {
	const auto* microphone = dynamic_cast<const MicrophoneAudioSource*>(source);
	return microphone != nullptr ? microphone->Volume() : 1.0F;
}

MicrophoneAudioProcessingStats
GetMicrophoneSourceProcessingStats(const MicrophoneAudioSourceInterface* source) {
	const auto* microphone = dynamic_cast<const MicrophoneAudioSource*>(source);
	return microphone != nullptr ? microphone->ProcessingStats() : MicrophoneAudioProcessingStats{};
}

bool SetMicrophoneSourceProcessingOptions(MicrophoneAudioSourceInterface* source,
                                          AudioSourceOptions options) {
	auto* microphone = dynamic_cast<MicrophoneAudioSource*>(source);
	return microphone != nullptr && microphone->SetProcessingOptions(options);
}

AudioSourceOptions
GetMicrophoneSourceProcessingOptions(const MicrophoneAudioSourceInterface* source) {
	const auto* microphone = dynamic_cast<const MicrophoneAudioSource*>(source);
	return microphone != nullptr ? microphone->ProcessingOptions() : AudioSourceOptions{};
}

MicrophoneAudioSourceInterface* CreateMicrophoneAudioSource(MicrophoneCaptureOptions options) {
	if (options.queue_size_ms == 0 || options.queue_size_ms % 10 != 0) {
		return nullptr;
	}
	auto source = std::make_unique<MicrophoneAudioSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
