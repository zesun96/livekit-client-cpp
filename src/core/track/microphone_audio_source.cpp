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

#include "../detail/global_task_queue.h"

#include <utility>
#include <vector>

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
                 std::uint32_t frames_per_channel, std::int64_t) {
	          OnAudioFrame(samples, sample_rate, channels, frames_per_channel);
          })) {}

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
}

bool MicrophoneAudioSource::IsCapturing() const { return capture_ && capture_->IsRunning(); }

std::string MicrophoneAudioSource::DeviceId() const {
	return capture_ ? capture_->DeviceId() : std::string{};
}

bool MicrophoneAudioSource::SwitchDevice(const std::string& device_id) {
	return !device_id.empty() && capture_ && capture_->SwitchDevice(device_id);
}

void MicrophoneAudioSource::SetMuted(bool muted) { muted_.store(muted); }

bool MicrophoneAudioSource::IsMuted() const { return muted_.load(); }

void MicrophoneAudioSource::OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
                                         std::uint32_t channels, std::uint32_t frames_per_channel) {
	if (samples == nullptr || sample_rate != kMicrophoneSampleRate ||
	    channels != kMicrophoneChannels || frames_per_channel == 0) {
		return;
	}
	if (!muted_.load()) {
		AudioSource::CaptureFrame(const_cast<std::int16_t*>(samples), sample_rate, channels,
		                          frames_per_channel);
		return;
	}
	thread_local std::vector<std::int16_t> silence;
	silence.assign(frames_per_channel, 0);
	AudioSource::CaptureFrame(silence.data(), sample_rate, channels, frames_per_channel);
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
