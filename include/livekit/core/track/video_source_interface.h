#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
#define _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_

#include "video_frame.h"

#include <memory>
#include <string>
#include <utility>

namespace livekit {
namespace core {

struct VideoSourceOptions {
	bool is_screencast = false;
};

struct CameraCaptureOptions {
	std::string device_id;
	uint32_t width = 1280;
	uint32_t height = 720;
	uint32_t frames_per_second = 30;
};

class VideoSourceInterface {
public:
	virtual ~VideoSourceInterface() = default;

	virtual bool CaptureFrame(const VideoFrame& frame) = 0;
	virtual uint32_t Width() const = 0;
	virtual uint32_t Height() const = 0;
};

class CameraVideoSourceInterface : public VideoSourceInterface {
public:
	~CameraVideoSourceInterface() override = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsCapturing() const = 0;
	virtual std::string DeviceId() const = 0;
	virtual bool SwitchDevice(const std::string& device_id) = 0;
};

VideoSourceInterface* CreateVideoSource(VideoSourceOptions options = {});

// Creates and starts a camera-backed source. Returns nullptr if the requested device cannot be
// opened. Use a VideoInput ID returned by EnumerateMediaDevices(); an empty ID selects the first
// available camera.
CameraVideoSourceInterface* CreateCameraVideoSource(CameraCaptureOptions options = {});

inline std::unique_ptr<VideoSourceInterface>
CreateVideoSourceUnique(VideoSourceOptions options = {}) {
	return std::unique_ptr<VideoSourceInterface>(CreateVideoSource(options));
}

inline std::unique_ptr<CameraVideoSourceInterface>
CreateCameraVideoSourceUnique(CameraCaptureOptions options = {}) {
	return std::unique_ptr<CameraVideoSourceInterface>(CreateCameraVideoSource(std::move(options)));
}

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
