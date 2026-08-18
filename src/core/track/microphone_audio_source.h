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

class MicrophoneAudioSource final : public MicrophoneAudioSourceInterface,
                                    public AudioSource,
                                    private AudioRenderObserver {
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
	bool BindAudioDevice(webrtc::scoped_refptr<AudioDevice> audio_device);

private:
	void OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
	                  std::uint32_t channels, std::uint32_t frames_per_channel,
	                  std::int64_t timestamp_us);
	void OnRenderData(const std::int16_t* samples, std::uint32_t sample_rate,
	                  std::uint32_t channels, std::uint32_t frames_per_channel) override;

	std::unique_ptr<capture::AudioCaptureAdapter> capture_;
	capture::WebRtcAudioProcessor processor_;
	std::mutex capture_buffer_mutex_;
	std::vector<std::int16_t> capture_buffer_;
	mutable std::mutex audio_device_mutex_;
	webrtc::scoped_refptr<AudioDevice> audio_device_;
	std::atomic_bool muted_{false};
	std::atomic<float> volume_{1.0F};
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
