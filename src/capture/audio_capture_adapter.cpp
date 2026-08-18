/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_capture_adapter.h"

#include "media_capture/audio_capture.h"
#include "media_capture/audio_device.h"

#include <utility>

namespace livekit::capture {

class AudioCaptureAdapter::Impl {
public:
	Impl(std::string device_id, AudioFrameCallback callback) {
		media_capture::AudioCaptureConfig config;
		config.device_id = std::move(device_id);
		capture_ = media_capture::CreateAudioCapture(
		    std::move(config), [callback = std::move(callback)](const auto& frame) {
			    callback(frame.data, frame.sample_rate, frame.channels, frame.frames_per_channel,
			             frame.timestamp_us);
		    });
	}

	std::unique_ptr<media_capture::AudioCapture> capture_;
};

AudioCaptureAdapter::AudioCaptureAdapter(std::string device_id, AudioFrameCallback callback)
    : impl_(std::make_unique<Impl>(std::move(device_id), std::move(callback))) {}

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
