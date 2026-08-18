/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "camera_capture_adapter.h"

#include "media_capture/camera_capture.h"
#include "media_capture/camera_device.h"

#include <utility>

namespace livekit::capture {

class CameraCaptureAdapter::Impl {
public:
	Impl(std::string device_id, std::uint32_t width, std::uint32_t height,
	     std::uint32_t frames_per_second, CameraFrameCallback callback) {
		media_capture::CameraCaptureConfig config;
		config.device_id = std::move(device_id);
		config.width = width;
		config.height = height;
		config.frames_per_second = frames_per_second;
		capture_ = media_capture::CreateCameraCapture(
		    std::move(config), [callback = std::move(callback)](const auto& frame) {
			    CapturedVideoFrame converted;
			    if (ConvertBgraToI420(frame.data, frame.width, frame.height, frame.row_stride_bytes,
			                          frame.timestamp_us, converted)) {
				    callback(converted);
			    }
		    });
	}

	std::unique_ptr<media_capture::CameraCapture> capture_;
};

CameraCaptureAdapter::CameraCaptureAdapter(std::string device_id, std::uint32_t width,
                                           std::uint32_t height, std::uint32_t frames_per_second,
                                           CameraFrameCallback callback)
    : impl_(std::make_unique<Impl>(std::move(device_id), width, height, frames_per_second,
                                   std::move(callback))) {}

CameraCaptureAdapter::~CameraCaptureAdapter() = default;

bool CameraCaptureAdapter::Start() { return impl_->capture_ && impl_->capture_->Start(); }

void CameraCaptureAdapter::Stop() noexcept {
	if (impl_->capture_) {
		impl_->capture_->Stop();
	}
}

bool CameraCaptureAdapter::IsRunning() const noexcept {
	return impl_->capture_ && impl_->capture_->IsRunning();
}

std::string CameraCaptureAdapter::DeviceId() const {
	return impl_->capture_ ? impl_->capture_->DeviceId() : std::string{};
}

bool CameraCaptureAdapter::SwitchDevice(std::string_view device_id) {
	return impl_->capture_ && impl_->capture_->SwitchDevice(device_id);
}

std::string CameraCaptureAdapter::LastError() const {
	return impl_->capture_ ? impl_->capture_->LastError() : "camera capture is unavailable";
}

std::vector<CameraDeviceInfo> EnumerateCameraDevices() {
	std::vector<CameraDeviceInfo> result;
	for (auto& device : media_capture::EnumerateCameraDevices()) {
		result.push_back({std::move(device.id), std::move(device.label)});
	}
	return result;
}

} // namespace livekit::capture
