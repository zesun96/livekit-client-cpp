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

class AudioSourceInterface {
public:
	virtual ~AudioSourceInterface() = default;

	virtual bool CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
	                          uint32_t samples_per_channel) = 0;
};

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
};

AudioSourceInterface* CreateAudioSource(AudioSourceOptions options, uint32_t sample_rate,
                                        uint32_t num_channels, uint32_t queue_size_ms);

// Creates and starts a 48 kHz mono microphone source. An empty device ID selects the system
// default input. The existing application-provided PCM source remains available separately.
MicrophoneAudioSourceInterface* CreateMicrophoneAudioSource(MicrophoneCaptureOptions options = {});

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

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_AUDIO_SOURCE_INTERFACE_H_
