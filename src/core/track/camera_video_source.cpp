/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "camera_video_source.h"

#include "modules/video_capture/video_capture_factory.h"

#include <array>
#include <limits>
#include <memory>
#include <utility>

namespace livekit::core {

CameraVideoSource::CameraVideoSource(CameraCaptureOptions options)
    : VideoSource({}), options_(std::move(options)) {}

CameraVideoSource::~CameraVideoSource() { Stop(); }

bool CameraVideoSource::CaptureFrame(const VideoFrame& frame) {
	return VideoSource::CaptureFrame(frame);
}

uint32_t CameraVideoSource::Width() const { return VideoSource::Width(); }

uint32_t CameraVideoSource::Height() const { return VideoSource::Height(); }

bool CameraVideoSource::Configure(const std::string& requested_id,
                                  webrtc::scoped_refptr<webrtc::VideoCaptureModule>& module,
                                  webrtc::VideoCaptureCapability& capability,
                                  std::string& resolved_id) const {
	resolved_id = requested_id;
	std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(
	    webrtc::VideoCaptureFactory::CreateDeviceInfo());
	if (device_info == nullptr || device_info->NumberOfDevices() == 0) {
		return false;
	}
	if (resolved_id.empty()) {
		std::array<char, 256> label{};
		std::array<char, 512> id{};
		if (device_info->GetDeviceName(0, label.data(), static_cast<uint32_t>(label.size()),
		                               id.data(), static_cast<uint32_t>(id.size())) != 0) {
			return false;
		}
		resolved_id = id.data();
	}

	webrtc::VideoCaptureCapability requested;
	if (options_.width == 0 || options_.height == 0 || options_.frames_per_second == 0 ||
	    options_.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
	    options_.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
	    options_.frames_per_second > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
		return false;
	}
	requested.width = static_cast<int32_t>(options_.width);
	requested.height = static_cast<int32_t>(options_.height);
	requested.maxFPS = static_cast<int32_t>(options_.frames_per_second);
	if (device_info->GetBestMatchedCapability(resolved_id.c_str(), requested, capability) < 0) {
		return false;
	}
	module = webrtc::VideoCaptureFactory::Create(resolved_id.c_str());
	return module != nullptr;
}

bool CameraVideoSource::StartLocked() {
	if (capture_module_ != nullptr && capture_module_->CaptureStarted()) {
		return true;
	}
	if (capture_module_ == nullptr) {
		std::string resolved_id;
		if (!Configure(options_.device_id, capture_module_, capability_, resolved_id)) {
			return false;
		}
		options_.device_id = std::move(resolved_id);
	}
	capture_module_->RegisterCaptureDataCallback(this);
	if (capture_module_->StartCapture(capability_) != 0) {
		capture_module_->DeRegisterCaptureDataCallback();
		return false;
	}
	return true;
}

bool CameraVideoSource::Start() {
	std::lock_guard<std::mutex> guard(mutex_);
	return StartLocked();
}

void CameraVideoSource::StopLocked() {
	if (capture_module_ == nullptr) {
		return;
	}
	if (capture_module_->CaptureStarted()) {
		capture_module_->StopCapture();
	}
	capture_module_->DeRegisterCaptureDataCallback();
}

void CameraVideoSource::Stop() {
	std::lock_guard<std::mutex> guard(mutex_);
	StopLocked();
}

bool CameraVideoSource::IsCapturing() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_module_ != nullptr && capture_module_->CaptureStarted();
}

std::string CameraVideoSource::DeviceId() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return options_.device_id;
}

bool CameraVideoSource::SwitchDevice(const std::string& device_id) {
	if (device_id.empty()) {
		return false;
	}
	std::lock_guard<std::mutex> guard(mutex_);
	if (device_id == options_.device_id) {
		return true;
	}
	webrtc::scoped_refptr<webrtc::VideoCaptureModule> replacement;
	webrtc::VideoCaptureCapability replacement_capability;
	std::string resolved_id;
	if (!Configure(device_id, replacement, replacement_capability, resolved_id)) {
		return false;
	}

	const bool was_capturing = capture_module_ != nullptr && capture_module_->CaptureStarted();
	StopLocked();
	if (was_capturing) {
		replacement->RegisterCaptureDataCallback(this);
		if (replacement->StartCapture(replacement_capability) != 0) {
			replacement->DeRegisterCaptureDataCallback();
			StartLocked();
			return false;
		}
	}
	capture_module_ = std::move(replacement);
	capability_ = replacement_capability;
	options_.device_id = std::move(resolved_id);
	return true;
}

void CameraVideoSource::OnFrame(const webrtc::VideoFrame& frame) { CaptureRtcFrame(frame); }

CameraVideoSourceInterface* CreateCameraVideoSource(CameraCaptureOptions options) {
	auto source = std::make_unique<CameraVideoSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
