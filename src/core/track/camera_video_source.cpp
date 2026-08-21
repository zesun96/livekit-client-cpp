/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "camera_video_source.h"

#include <utility>

namespace livekit::core {

CameraVideoSource::CameraVideoSource(CameraCaptureOptions options)
    : VideoSource({}), options_(std::move(options)), capture_(CreateAdapter(options_.device_id)) {}

CameraVideoSource::~CameraVideoSource() { Stop(); }

bool CameraVideoSource::CaptureFrame(const VideoFrame& frame) {
	return VideoSource::CaptureFrame(frame);
}

uint32_t CameraVideoSource::Width() const { return VideoSource::Width(); }

uint32_t CameraVideoSource::Height() const { return VideoSource::Height(); }

std::unique_ptr<capture::CameraCaptureAdapter>
CameraVideoSource::CreateAdapter(const std::string& device_id) {
	return std::make_unique<capture::CameraCaptureAdapter>(
	    device_id, options_.width, options_.height, options_.frames_per_second,
	    [this](const capture::CapturedVideoFrame& frame) { OnFrame(frame); });
}

bool CameraVideoSource::Start() {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ && capture_->Start();
}

void CameraVideoSource::Stop() {
	std::lock_guard<std::mutex> guard(mutex_);
	if (capture_) {
		capture_->Stop();
	}
}

bool CameraVideoSource::IsCapturing() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ && capture_->IsRunning();
}

std::string CameraVideoSource::DeviceId() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return capture_ ? capture_->DeviceId() : std::string{};
}

bool CameraVideoSource::SwitchDevice(const std::string& device_id) {
	if (device_id.empty()) {
		return false;
	}
	std::lock_guard<std::mutex> guard(mutex_);
	if (!capture_ || !capture_->SwitchDevice(device_id)) {
		return false;
	}
	options_.device_id = capture_->DeviceId();
	return true;
}

void CameraVideoSource::OnFrame(const capture::CapturedVideoFrame& frame) {
	VideoFrame video_frame;
	video_frame.data = frame.i420;
	video_frame.width = frame.width;
	video_frame.height = frame.height;
	video_frame.timestamp_us = frame.timestamp_us;
	video_frame.rotation = static_cast<VideoRotation>(frame.rotation_degrees);
	VideoSource::CaptureFrame(video_frame);
}

CameraVideoSourceInterface* CreateCameraVideoSource(CameraCaptureOptions options) {
	if (options.width == 0 || options.height == 0 || options.frames_per_second == 0 ||
	    options.frames_per_second > 240) {
		return nullptr;
	}
	auto source = std::make_unique<CameraVideoSource>(std::move(options));
	if (!source->Start()) {
		return nullptr;
	}
	return source.release();
}

} // namespace livekit::core
