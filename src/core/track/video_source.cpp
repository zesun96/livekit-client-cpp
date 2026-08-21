#include "video_source.h"

#include "api/make_ref_counted.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"

#include <limits>

namespace livekit {
namespace core {

VideoSource::InternalSource::InternalSource(bool is_screencast) : is_screencast_(is_screencast) {}

webrtc::MediaSourceInterface::SourceState VideoSource::InternalSource::state() const {
	return webrtc::MediaSourceInterface::SourceState::kLive;
}

bool VideoSource::InternalSource::remote() const { return false; }

bool VideoSource::InternalSource::is_screencast() const { return is_screencast_; }

std::optional<bool> VideoSource::InternalSource::needs_denoising() const { return std::nullopt; }

bool VideoSource::InternalSource::CaptureFrame(const VideoFrame& frame) {
	if (frame.width == 0 || frame.height == 0 || frame.width % 2 != 0 || frame.height % 2 != 0 ||
	    frame.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
	    frame.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
	    (frame.rotation != VideoRotation::Rotation0 &&
	     frame.rotation != VideoRotation::Rotation90 &&
	     frame.rotation != VideoRotation::Rotation180 &&
	     frame.rotation != VideoRotation::Rotation270)) {
		return false;
	}
	const std::size_t y_size = static_cast<std::size_t>(frame.width) * frame.height;
	const std::size_t chroma_size = y_size / 4;
	if (frame.data.size() != y_size + chroma_size * 2) {
		return false;
	}

	const int width = static_cast<int>(frame.width);
	const int height = static_cast<int>(frame.height);
	auto buffer = webrtc::I420Buffer::Copy(width, height, frame.data.data(), width,
	                                       frame.data.data() + y_size, width / 2,
	                                       frame.data.data() + y_size + chroma_size, width / 2);
	if (!buffer) {
		return false;
	}
	const auto rtc_frame = webrtc::VideoFrame::Builder()
	                           .set_video_frame_buffer(buffer)
	                           .set_timestamp_us(frame.timestamp_us)
	                           .set_rotation(static_cast<webrtc::VideoRotation>(frame.rotation))
	                           .build();
	OnFrame(rtc_frame);
	return true;
}

void VideoSource::InternalSource::CaptureFrame(const webrtc::VideoFrame& frame) { OnFrame(frame); }

VideoSource::VideoSource(VideoSourceOptions options)
    : source_(webrtc::make_ref_counted<InternalSource>(options.is_screencast)) {}

bool VideoSource::CaptureFrame(const VideoFrame& frame) {
	if (!source_->CaptureFrame(frame)) {
		return false;
	}
	width_.store(frame.width);
	height_.store(frame.height);
	return true;
}

uint32_t VideoSource::Width() const { return width_.load(); }

uint32_t VideoSource::Height() const { return height_.load(); }

webrtc::scoped_refptr<VideoSource::InternalSource> VideoSource::Get() const { return source_; }

void VideoSource::CaptureRtcFrame(const webrtc::VideoFrame& frame) {
	source_->CaptureFrame(frame);
	width_.store(static_cast<uint32_t>(frame.width()));
	height_.store(static_cast<uint32_t>(frame.height()));
}

VideoSourceInterface* CreateVideoSource(VideoSourceOptions options) {
	return new VideoSource(options);
}

} // namespace core
} // namespace livekit
