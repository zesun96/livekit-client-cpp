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
      capture_(std::make_unique<capture::AudioCaptureAdapter>(
          std::move(options.device_id),
          [this](const std::int16_t* samples, std::uint32_t sample_rate, std::uint32_t channels,
                 std::uint32_t frames_per_channel, std::int64_t timestamp_us) {
	          OnAudioFrame(samples, sample_rate, channels, frames_per_channel, timestamp_us);
          })),
      processor_(options.processing.echo_cancellation, options.processing.auto_gain_control,
                 options.processing.noise_suppression) {}

MicrophoneAudioSource::~MicrophoneAudioSource() {
	Stop();
	std::lock_guard<std::mutex> guard(audio_device_mutex_);
	if (audio_device_ != nullptr) {
		audio_device_->RemoveRenderObserver(this);
	}
}

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

bool MicrophoneAudioSource::BindAudioDevice(webrtc::scoped_refptr<AudioDevice> audio_device) {
	if (audio_device == nullptr) {
		return false;
	}
	std::lock_guard<std::mutex> guard(audio_device_mutex_);
	if (audio_device_ != nullptr && audio_device_ != audio_device) {
		return false;
	}
	if (audio_device_ == nullptr) {
		audio_device->AddRenderObserver(this);
	}
	audio_device_ = std::move(audio_device);
	return true;
}

void MicrophoneAudioSource::OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
                                         std::uint32_t channels, std::uint32_t frames_per_channel,
                                         std::int64_t) {
	if (samples == nullptr || sample_rate != kMicrophoneSampleRate ||
	    channels != kMicrophoneChannels || frames_per_channel == 0) {
		return;
	}
	std::lock_guard<std::mutex> guard(capture_buffer_mutex_);
	capture_buffer_.insert(capture_buffer_.end(), samples, samples + frames_per_channel);
	while (capture_buffer_.size() >= kMicrophoneSampleRate / 100) {
		std::array<std::int16_t, kMicrophoneSampleRate / 100> processed;
		std::copy_n(capture_buffer_.begin(), processed.size(), processed.begin());
		capture_buffer_.erase(capture_buffer_.begin(), capture_buffer_.begin() + processed.size());
		if (!processor_.ProcessCapture(processed, sample_rate, channels)) {
			continue;
		}
		const float gain = muted_.load() ? 0.0F : volume_.load();
		if (capture::ApplyAudioGain(processed, processed, gain)) {
			AudioSource::CaptureFrame(processed.data(), sample_rate, channels, processed.size());
		}
	}
}

void MicrophoneAudioSource::OnRenderData(const std::int16_t* samples, std::uint32_t sample_rate,
                                         std::uint32_t channels, std::uint32_t frames_per_channel) {
	if (samples == nullptr || frames_per_channel == 0) {
		return;
	}
	processor_.ProcessRender({samples, frames_per_channel * channels}, sample_rate, channels);
}

bool SetMicrophoneSourceVolume(MicrophoneAudioSourceInterface* source, float volume) {
	auto* microphone = dynamic_cast<MicrophoneAudioSource*>(source);
	return microphone != nullptr && microphone->SetVolume(volume);
}

float GetMicrophoneSourceVolume(const MicrophoneAudioSourceInterface* source) {
	const auto* microphone = dynamic_cast<const MicrophoneAudioSource*>(source);
	return microphone != nullptr ? microphone->Volume() : 1.0F;
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
