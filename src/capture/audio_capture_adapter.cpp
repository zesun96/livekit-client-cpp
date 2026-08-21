/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_capture_adapter.h"

#include "media_capture/audio_capture.h"
#include "media_capture/audio_device.h"
#include "media_capture/audio_playback.h"

#include <utility>

namespace livekit::capture {

class AudioCaptureAdapter::Impl {
public:
	Impl(std::string device_id, AudioFrameCallback callback, bool system_audio) {
		media_capture::AudioCaptureConfig config;
		config.device_id = std::move(device_id);
		config.channels = system_audio ? 2 : 1;
		media_capture::AudioFrameCallback frame_callback =
		    [callback = std::move(callback)](const auto& frame) {
			    callback(frame.data, frame.sample_rate, frame.channels, frame.frames_per_channel,
			             frame.timestamp_us);
		    };
		capture_ = system_audio ? media_capture::CreateSystemAudioCapture(std::move(config),
		                                                                  std::move(frame_callback))
		                        : media_capture::CreateAudioCapture(std::move(config),
		                                                            std::move(frame_callback));
	}

	std::unique_ptr<media_capture::AudioCapture> capture_;
};

AudioCaptureAdapter::AudioCaptureAdapter(std::string device_id, AudioFrameCallback callback,
                                         bool system_audio)
    : impl_(std::make_unique<Impl>(std::move(device_id), std::move(callback), system_audio)) {}

AudioCaptureAdapter::~AudioCaptureAdapter() = default;

bool AudioCaptureAdapter::Start() { return impl_->capture_ && impl_->capture_->Start(); }

void AudioCaptureAdapter::Stop() noexcept {
	if (impl_->capture_) {
		impl_->capture_->Stop();
	}
}

bool AudioCaptureAdapter::IsRunning() const noexcept {
	return impl_->capture_ && impl_->capture_->IsRunning();
}

std::string AudioCaptureAdapter::DeviceId() const {
	return impl_->capture_ ? impl_->capture_->DeviceId() : std::string{};
}

bool AudioCaptureAdapter::SwitchDevice(std::string_view device_id) {
	return impl_->capture_ && impl_->capture_->SwitchDevice(device_id);
}

std::string AudioCaptureAdapter::LastError() const {
	return impl_->capture_ ? impl_->capture_->LastError() : "audio capture is unavailable";
}

class AudioPlaybackAdapter::Impl {
public:
	explicit Impl(std::string device_id) {
		media_capture::AudioPlaybackConfig config;
		config.device_id = std::move(device_id);
		playback_ = media_capture::CreateAudioPlayback(std::move(config));
	}

	std::unique_ptr<media_capture::AudioPlayback> playback_;
};

AudioPlaybackAdapter::AudioPlaybackAdapter(std::string device_id)
    : impl_(std::make_unique<Impl>(std::move(device_id))) {}

AudioPlaybackAdapter::~AudioPlaybackAdapter() = default;

bool AudioPlaybackAdapter::Start() { return impl_->playback_ && impl_->playback_->Start(); }

void AudioPlaybackAdapter::Stop() noexcept {
	if (impl_->playback_) {
		impl_->playback_->Stop();
	}
}

bool AudioPlaybackAdapter::IsRunning() const noexcept {
	return impl_->playback_ && impl_->playback_->IsRunning();
}

std::string AudioPlaybackAdapter::DeviceId() const {
	return impl_->playback_ ? impl_->playback_->DeviceId() : std::string{};
}

bool AudioPlaybackAdapter::SwitchDevice(std::string_view device_id) {
	return impl_->playback_ && impl_->playback_->SwitchDevice(device_id);
}

bool AudioPlaybackAdapter::QueueFrame(const std::int16_t* samples, std::uint32_t sample_rate,
                                      std::uint32_t channels, std::uint32_t frames_per_channel) {
	return impl_->playback_ &&
	       impl_->playback_->QueueFrame({samples, sample_rate, channels, frames_per_channel, 0});
}

bool AudioPlaybackAdapter::SetVolume(float volume) {
	return impl_->playback_ && impl_->playback_->SetVolume(volume);
}

float AudioPlaybackAdapter::Volume() const noexcept {
	return impl_->playback_ ? impl_->playback_->Volume() : 0.0F;
}

void AudioPlaybackAdapter::SetMuted(bool muted) noexcept {
	if (impl_->playback_) {
		impl_->playback_->SetMuted(muted);
	}
}

bool AudioPlaybackAdapter::IsMuted() const noexcept {
	return impl_->playback_ && impl_->playback_->IsMuted();
}

AudioPlaybackStats AudioPlaybackAdapter::Stats() const noexcept {
	AudioPlaybackStats result;
	if (!impl_->playback_) {
		return result;
	}
	const auto stats = impl_->playback_->Stats();
	result.queued_frames = stats.queued_frames;
	result.played_frames = stats.played_frames;
	result.dropped_frames = stats.dropped_frames;
	result.underrun_frames = stats.underrun_frames;
	result.buffered_duration_ms = stats.buffered_duration_ms;
	result.device_latency_ms = stats.device_latency_ms;
	result.estimated_delay_ms = stats.estimated_delay_ms;
	return result;
}

std::string AudioPlaybackAdapter::LastError() const {
	return impl_->playback_ ? impl_->playback_->LastError() : "audio playback is unavailable";
}

std::vector<AudioDeviceInfo> EnumerateAudioDevices() {
	const auto source = media_capture::EnumerateAudioDevices();
	std::vector<AudioDeviceInfo> result;
	result.reserve(source.size());
	for (const auto& device : source) {
		result.push_back({device.id, device.label,
		                  device.kind == media_capture::AudioDeviceKind::Input
		                      ? AudioDeviceKind::Input
		                      : AudioDeviceKind::Output,
		                  device.is_default});
	}
	return result;
}

} // namespace livekit::capture
