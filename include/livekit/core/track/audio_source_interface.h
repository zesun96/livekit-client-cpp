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

#ifndef _LKC_CORE_TRACK_AUDIO_SOURCE_INTERFACE_H_
#define _LKC_CORE_TRACK_AUDIO_SOURCE_INTERFACE_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <utility>

namespace livekit {
namespace core {

struct AudioSourceOptions {
	bool echo_cancellation = false;
	bool auto_gain_control = false;
	bool noise_suppression = false;
};

struct MicrophoneCaptureOptions {
	std::string device_id;
	AudioSourceOptions processing{true, true, true};
	uint32_t queue_size_ms = 200;
};

struct SystemAudioCaptureOptions {
	std::string device_id;
	uint32_t queue_size_ms = 200;
};

struct MicrophoneAudioProcessingStats {
	uint64_t capture_frames_processed = 0;
	uint64_t render_frames_processed = 0;
	uint64_t capture_processing_errors = 0;
	uint64_t render_processing_errors = 0;
	uint64_t frames_dropped = 0;
	bool echo_cancellation_enabled = false;
	// The following AEC quality metrics are populated only after WebRTC has observed enough
	// correlated capture and render audio. Check the matching availability flag before use.
	bool echo_return_loss_available = false;
	double echo_return_loss_db = 0.0;
	bool echo_return_loss_enhancement_available = false;
	double echo_return_loss_enhancement_db = 0.0;
	bool residual_echo_likelihood_available = false;
	double residual_echo_likelihood = 0.0;
	bool residual_echo_likelihood_recent_max_available = false;
	double residual_echo_likelihood_recent_max = 0.0;
	bool delay_median_available = false;
	int32_t delay_median_ms = 0;
	bool delay_standard_deviation_available = false;
	int32_t delay_standard_deviation_ms = 0;
	bool delay_available = false;
	int32_t delay_ms = 0;
};

class AudioSourceInterface {
public:
	virtual ~AudioSourceInterface() = default;

	virtual bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                          uint32_t samples_per_channel) = 0;
};

class MicrophoneAudioSourceInterface;
bool SetMicrophoneSourceVolume(MicrophoneAudioSourceInterface* source, float volume);
float GetMicrophoneSourceVolume(const MicrophoneAudioSourceInterface* source);
MicrophoneAudioProcessingStats
GetMicrophoneSourceProcessingStats(const MicrophoneAudioSourceInterface* source);
bool SetMicrophoneSourceProcessingOptions(MicrophoneAudioSourceInterface* source,
                                          AudioSourceOptions options);
AudioSourceOptions
GetMicrophoneSourceProcessingOptions(const MicrophoneAudioSourceInterface* source);

class MicrophoneAudioSourceInterface : public AudioSourceInterface {
public:
	~MicrophoneAudioSourceInterface() override = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsCapturing() const = 0;
	virtual std::string DeviceId() const = 0;
	virtual bool SwitchDevice(const std::string& device_id) = 0;
	virtual void SetMuted(bool muted) = 0;
	virtual bool IsMuted() const = 0;
	// Normalized software input gain. Implementations that do not support gain return false.
	bool SetVolume(float volume) { return SetMicrophoneSourceVolume(this, volume); }
	float Volume() const { return GetMicrophoneSourceVolume(this); }
	MicrophoneAudioProcessingStats ProcessingStats() const {
		return GetMicrophoneSourceProcessingStats(this);
	}
	bool SetProcessingOptions(AudioSourceOptions options) {
		return SetMicrophoneSourceProcessingOptions(this, options);
	}
	AudioSourceOptions ProcessingOptions() const {
		return GetMicrophoneSourceProcessingOptions(this);
	}
};

AudioSourceInterface* CreateAudioSource(AudioSourceOptions options, uint32_t sample_rate,
                                        uint32_t num_channels, uint32_t queue_size_ms);

// Creates and starts a 48 kHz mono microphone source. An empty device ID selects the system
// default input. The existing application-provided PCM source remains available separately.
MicrophoneAudioSourceInterface* CreateMicrophoneAudioSource(MicrophoneCaptureOptions options = {});

class SystemAudioSourceInterface : public AudioSourceInterface {
public:
	~SystemAudioSourceInterface() override = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsCapturing() const = 0;
	virtual std::string DeviceId() const = 0;
	virtual bool SwitchDevice(const std::string& device_id) = 0;
};

// Creates and starts a 48 kHz stereo source that captures an output device. This is independent
// from CreateAudioSource(), which continues to accept application-provided PCM.
SystemAudioSourceInterface* CreateSystemAudioSource(SystemAudioCaptureOptions options = {});

inline std::unique_ptr<AudioSourceInterface> CreateAudioSourceUnique(AudioSourceOptions options,
                                                                     uint32_t sample_rate,
                                                                     uint32_t num_channels,
                                                                     uint32_t queue_size_ms) {
	return std::unique_ptr<AudioSourceInterface>(
	    CreateAudioSource(options, sample_rate, num_channels, queue_size_ms));
}

inline std::unique_ptr<MicrophoneAudioSourceInterface>
CreateMicrophoneAudioSourceUnique(MicrophoneCaptureOptions options = {}) {
	return std::unique_ptr<MicrophoneAudioSourceInterface>(
	    CreateMicrophoneAudioSource(std::move(options)));
}

inline std::unique_ptr<SystemAudioSourceInterface>
CreateSystemAudioSourceUnique(SystemAudioCaptureOptions options = {}) {
	return std::unique_ptr<SystemAudioSourceInterface>(CreateSystemAudioSource(std::move(options)));
}

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_AUDIO_SOURCE_INTERFACE_H_
