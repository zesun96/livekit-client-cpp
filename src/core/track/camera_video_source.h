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

#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "modules/video_capture/video_capture.h"

#include <mutex>
#include <string>

namespace livekit::core {

class CameraVideoSource final : public CameraVideoSourceInterface,
                                public VideoSource,
                                private webrtc::VideoSinkInterface<webrtc::VideoFrame> {
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
	void OnFrame(const webrtc::VideoFrame& frame) override;
	bool Configure(const std::string& device_id,
	               webrtc::scoped_refptr<webrtc::VideoCaptureModule>& module,
	               webrtc::VideoCaptureCapability& capability, std::string& resolved_id) const;
	bool StartLocked();
	void StopLocked();

	mutable std::mutex mutex_;
	CameraCaptureOptions options_;
	webrtc::scoped_refptr<webrtc::VideoCaptureModule> capture_module_;
	webrtc::VideoCaptureCapability capability_;
};

} // namespace livekit::core

#endif // LKC_CORE_TRACK_CAMERA_VIDEO_SOURCE_H
