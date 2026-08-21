/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "system_audio_source.h"

#include "../detail/global_task_queue.h"

#include <memory>
#include <utility>

namespace livekit::core {
namespace {

constexpr uint32_t kSystemAudioSampleRate = 48000;
constexpr uint32_t kSystemAudioChannels = 2;

} // namespace

SystemAudioSource::SystemAudioSource(SystemAudioCaptureOptions options)
    : AudioSource({}, kSystemAudioSampleRate, kSystemAudioChannels, options.queue_size_ms,
                  GetGlobalTaskQueueFactory()),
      capture_(std::make_unique<capture::AudioCaptureAdapter>(
          std::move(options.device_id),
          [this](const std::int16_t* samples, std::uint32_t sample_rate, std::uint32_t channels,
                 std::uint32_t frames_per_channel, std::int64_t) {
	          AudioSource::CaptureFrame(const_cast<std::int16_t*>(samples), sample_rate, channels,
	                                    frames_per_channel);
          },
          true)) {}

SystemAudioSource::~SystemAudioSource() { Stop(); }

bool SystemAudioSource::CaptureFrame(void* audio_data, uint32_t sample_rate, uint32_t num_channels,
                                     uint32_t samples_per_channel) {
	return AudioSource::CaptureFrame(audio_data, sample_rate, num_channels, samples_per_channel);
}

bool SystemAudioSource::Start() { return capture_ && capture_->Start(); }

void SystemAudioSource::Stop() {
	if (capture_) {
		capture_->Stop();
	}
	ClearBuffer();
}

bool SystemAudioSource::IsCapturing() const { return capture_ && capture_->IsRunning(); }

std::string SystemAudioSource::DeviceId() const {
	return capture_ ? capture_->DeviceId() : std::string{};
}

bool SystemAudioSource::SwitchDevice(const std::string& device_id) {
	if (device_id.empty() || !capture_) {
		return false;
	}
	const bool switched = capture_->SwitchDevice(device_id);
	ClearBuffer();
	return switched;
}

SystemAudioSourceInterface* CreateSystemAudioSource(SystemAudioCaptureOptions options) {
	if (options.queue_size_ms == 0 || options.queue_size_ms % 10 != 0) {
		return nullptr;
	}
	auto source = std::make_unique<SystemAudioSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
