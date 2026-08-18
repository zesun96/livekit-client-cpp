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

#ifndef LKC_CORE_TRACK_CAMERA_VIDEO_SOURCE_H
#define LKC_CORE_TRACK_CAMERA_VIDEO_SOURCE_H

#include "video_source.h"

#include "../../capture/camera_capture_adapter.h"

#include <memory>
#include <mutex>
#include <string>

namespace livekit::core {

class CameraVideoSource final : public CameraVideoSourceInterface, public VideoSource {
public:
	explicit CameraVideoSource(CameraCaptureOptions options);
	~CameraVideoSource() override;

	bool CaptureFrame(const VideoFrame& frame) override;
	uint32_t Width() const override;
	uint32_t Height() const override;
	bool Start() override;
	void Stop() override;
	bool IsCapturing() const override;
	std::string DeviceId() const override;
	bool SwitchDevice(const std::string& device_id) override;

private:
	std::unique_ptr<capture::CameraCaptureAdapter> CreateAdapter(const std::string& device_id);
	void OnFrame(const capture::CapturedVideoFrame& frame);

	mutable std::mutex mutex_;
	CameraCaptureOptions options_;
	std::unique_ptr<capture::CameraCaptureAdapter> capture_;
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_CAMERA_VIDEO_SOURCE_H
