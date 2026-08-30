#include "video_source.h"

#include "../detail/frame_metadata.h"
#include "public_video_frame_converter.h"

#include "api/make_ref_counted.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "rtc_base/time_utils.h"

namespace livekit {
namespace core {

VideoSource::InternalSource::InternalSource(
    bool is_screencast, std::shared_ptr<detail::FrameMetadataStore> metadata_store)
    : is_screencast_(is_screencast), metadata_store_(std::move(metadata_store)) {}

webrtc::MediaSourceInterface::SourceState VideoSource::InternalSource::state() const {
	return webrtc::MediaSourceInterface::SourceState::kLive;
}

bool VideoSource::InternalSource::remote() const { return false; }

bool VideoSource::InternalSource::is_screencast() const { return is_screencast_; }

std::optional<bool> VideoSource::InternalSource::needs_denoising() const { return std::nullopt; }

bool VideoSource::InternalSource::CaptureFrame(const VideoFrame& frame) {
	if (!detail::IsValidVideoFrameMetadata(frame.metadata)) {
		return false;
	}
	std::vector<std::uint8_t> i420;
	if (!detail::ConvertVideoFrameToI420(frame, i420)) {
		return false;
	}
	const std::size_t y_size = static_cast<std::size_t>(frame.width) * frame.height;
	const std::size_t chroma_width = (static_cast<std::size_t>(frame.width) + 1U) / 2U;
	const std::size_t chroma_height = (static_cast<std::size_t>(frame.height) + 1U) / 2U;
	const std::size_t chroma_size = chroma_width * chroma_height;

	const int width = static_cast<int>(frame.width);
	const int height = static_cast<int>(frame.height);
	auto buffer = webrtc::I420Buffer::Copy(
	    width, height, i420.data(), width, i420.data() + y_size, static_cast<int>(chroma_width),
	    i420.data() + y_size + chroma_size, static_cast<int>(chroma_width));
	if (!buffer) {
		return false;
	}
	const auto timestamp_us = frame.timestamp_us != 0 ? frame.timestamp_us : webrtc::TimeMicros();
	const auto rtp_timestamp = detail::VideoRtpTimestampFromMicros(timestamp_us);
	const auto rtc_frame = webrtc::VideoFrame::Builder()
	                           .set_video_frame_buffer(buffer)
	                           .set_timestamp_us(timestamp_us)
	                           .set_rtp_timestamp(rtp_timestamp)
	                           .set_rotation(static_cast<webrtc::VideoRotation>(frame.rotation))
	                           .build();
	if (frame.metadata) {
		metadata_store_->Store(rtp_timestamp, *frame.metadata, timestamp_us);
	}
	OnFrame(rtc_frame);
	return true;
}

void VideoSource::InternalSource::CaptureFrame(const webrtc::VideoFrame& frame) { OnFrame(frame); }

VideoSource::VideoSource(VideoSourceOptions options)
    : metadata_store_(std::make_shared<detail::FrameMetadataStore>()),
      source_(webrtc::make_ref_counted<InternalSource>(options.is_screencast, metadata_store_)) {}

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

std::shared_ptr<detail::FrameMetadataStore> VideoSource::GetFrameMetadataStore() const {
	return metadata_store_;
}

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
