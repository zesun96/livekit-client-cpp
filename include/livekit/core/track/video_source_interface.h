#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
#define _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_

#include "video_frame.h"

#include <memory>

namespace livekit {
namespace core {

struct VideoSourceOptions {
	bool is_screencast = false;
};

class VideoSourceInterface {
public:
	virtual ~VideoSourceInterface() = default;

	virtual bool CaptureFrame(const VideoFrame& frame) = 0;
	virtual uint32_t Width() const = 0;
	virtual uint32_t Height() const = 0;
};

VideoSourceInterface* CreateVideoSource(VideoSourceOptions options = {});

inline std::unique_ptr<VideoSourceInterface>
CreateVideoSourceUnique(VideoSourceOptions options = {}) {
	return std::unique_ptr<VideoSourceInterface>(CreateVideoSource(options));
}

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_SOURCE_INTERFACE_H_
