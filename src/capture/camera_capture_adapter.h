/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "video_frame_converter.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace livekit::capture {

struct CameraDeviceInfo {
	std::string id;
	std::string label;
};

using CameraFrameCallback = std::function<void(const CapturedVideoFrame& frame)>;

class CameraCaptureAdapter {
public:
	CameraCaptureAdapter(std::string device_id, std::uint32_t width, std::uint32_t height,
	                     std::uint32_t frames_per_second, CameraFrameCallback callback);
	~CameraCaptureAdapter();

	CameraCaptureAdapter(const CameraCaptureAdapter&) = delete;
	CameraCaptureAdapter& operator=(const CameraCaptureAdapter&) = delete;

	bool Start();
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::string DeviceId() const;
	bool SwitchDevice(std::string_view device_id);
	std::string LastError() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

std::vector<CameraDeviceInfo> EnumerateCameraDevices();

} // namespace livekit::capture
