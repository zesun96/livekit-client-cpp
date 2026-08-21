/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#ifndef LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
#define LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H

#include "audio_source.h"

#include "../../capture/audio_capture_adapter.h"
#include "../../capture/webrtc_audio_processor.h"
#include "../detail/audio_device.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace livekit::core {

class MicrophoneAudioSource final : public MicrophoneAudioSourceInterface, public AudioSource {
public:
	explicit MicrophoneAudioSource(MicrophoneCaptureOptions options);
	~MicrophoneAudioSource() override;

	bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                  uint32_t samples_per_channel) override;
	bool Start() override;
	void Stop() override;
	bool IsCapturing() const override;
	std::string DeviceId() const override;
	bool SwitchDevice(const std::string& device_id) override;
	void SetMuted(bool muted) override;
	bool IsMuted() const override;
	bool SetVolume(float volume);
	float Volume() const;
	MicrophoneAudioProcessingStats ProcessingStats() const;
	bool BindAudioDevice(webrtc::scoped_refptr<AudioDevice> audio_device);

private:
	void OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
	                  std::uint32_t channels, std::uint32_t frames_per_channel,
	                  std::int64_t timestamp_us) noexcept;
	AudioSourceOptions processing_options_;
	std::unique_ptr<capture::AudioCaptureAdapter> capture_;
	capture::WebRtcAudioProcessor processor_;
	std::mutex capture_buffer_mutex_;
	std::vector<std::int16_t> capture_buffer_;
	std::vector<std::int16_t> render_processing_buffer_;
	uint64_t last_render_sequence_ = 0;
	mutable std::mutex audio_device_mutex_;
	webrtc::scoped_refptr<AudioDevice> audio_device_;
	std::atomic_bool muted_{false};
	std::atomic<float> volume_{1.0F};
	std::atomic<uint64_t> capture_frames_processed_{0};
	std::atomic<uint64_t> render_frames_processed_{0};
	std::atomic<uint64_t> capture_processing_errors_{0};
	std::atomic<uint64_t> render_processing_errors_{0};
	std::atomic<uint64_t> frames_dropped_{0};
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
