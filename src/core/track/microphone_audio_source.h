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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

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

private:
	void OnAudioFrame(const std::int16_t* samples, std::uint32_t sample_rate,
	                  std::uint32_t channels, std::uint32_t frames_per_channel);

	std::unique_ptr<capture::AudioCaptureAdapter> capture_;
	std::atomic_bool muted_{false};
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_MICROPHONE_AUDIO_SOURCE_H
