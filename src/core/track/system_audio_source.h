/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "audio_source.h"

#include "../../capture/audio_capture_adapter.h"

#include <memory>
#include <string>

namespace livekit::core {

class SystemAudioSource final : public SystemAudioSourceInterface, public AudioSource {
public:
	explicit SystemAudioSource(SystemAudioCaptureOptions options);
	~SystemAudioSource() override;

	bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                  uint32_t samples_per_channel) override;
	bool Start() override;
	void Stop() override;
	bool IsCapturing() const override;
	std::string DeviceId() const override;
	bool SwitchDevice(const std::string& device_id) override;

private:
	std::unique_ptr<capture::AudioCaptureAdapter> capture_;
};

} // namespace livekit::core
