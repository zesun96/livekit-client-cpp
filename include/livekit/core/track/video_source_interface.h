#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
#define _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_

#include "video_frame.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

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

enum class ScreenCaptureSourceKind {
	Monitor,
	Window,
};

struct ScreenCaptureSourceInfo {
	std::string id;
	std::string label;
	ScreenCaptureSourceKind kind = ScreenCaptureSourceKind::Monitor;
	int32_t x = 0;
	int32_t y = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct ScreenCaptureOptions {
	std::string source_id;
	uint32_t frames_per_second = 15;
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

class ScreenVideoSourceInterface : public VideoSourceInterface {
public:
	~ScreenVideoSourceInterface() override = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsCapturing() const = 0;
	virtual std::string SourceId() const = 0;
	virtual bool SwitchSource(const std::string& source_id) = 0;
};

VideoSourceInterface* CreateVideoSource(VideoSourceOptions options = {});

// Creates and starts a camera-backed source. Returns nullptr if the requested device cannot be
// opened. Use a VideoInput ID returned by EnumerateMediaDevices(); an empty ID selects the first
// available camera.
CameraVideoSourceInterface* CreateCameraVideoSource(CameraCaptureOptions options = {});

// Returns currently shareable monitors and windows. IDs are stable for the lifetime of the
// corresponding OS source and can be passed to CreateScreenVideoSource().
std::vector<ScreenCaptureSourceInfo> EnumerateScreenCaptureSources();
// Creates and starts a monitor- or window-backed source. Returns nullptr when the source cannot be
// opened or the requested frame rate is outside the supported 1-60 range.
ScreenVideoSourceInterface* CreateScreenVideoSource(ScreenCaptureOptions options);

inline std::unique_ptr<VideoSourceInterface>
CreateVideoSourceUnique(VideoSourceOptions options = {}) {
	return std::unique_ptr<VideoSourceInterface>(CreateVideoSource(options));
}

inline std::unique_ptr<CameraVideoSourceInterface>
CreateCameraVideoSourceUnique(CameraCaptureOptions options = {}) {
	return std::unique_ptr<CameraVideoSourceInterface>(CreateCameraVideoSource(std::move(options)));
}

inline std::unique_ptr<ScreenVideoSourceInterface>
CreateScreenVideoSourceUnique(ScreenCaptureOptions options) {
	return std::unique_ptr<ScreenVideoSourceInterface>(CreateScreenVideoSource(std::move(options)));
}

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
