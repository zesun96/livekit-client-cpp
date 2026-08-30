#pragma once

#ifndef _LKC_CORE_TRACK_VIDEO_SOURCE_H_
#define _LKC_CORE_TRACK_VIDEO_SOURCE_H_

#include "livekit/core/track/video_source_interface.h"

#include "api/scoped_refptr.h"
#include "media/base/adapted_video_track_source.h"

#include <atomic>
#include <memory>

namespace livekit::core::detail {
class FrameMetadataStore;
}

namespace livekit {
namespace core {

class VideoSource : public VideoSourceInterface {
public:
	class InternalSource : public webrtc::AdaptedVideoTrackSource {
	public:
		InternalSource(bool is_screencast,
		               std::shared_ptr<detail::FrameMetadataStore> metadata_store);

		webrtc::MediaSourceInterface::SourceState state() const override;
		bool remote() const override;
		bool is_screencast() const override;
		std::optional<bool> needs_denoising() const override;

		bool CaptureFrame(const VideoFrame& frame);
		void CaptureFrame(const webrtc::VideoFrame& frame);

	private:
		bool is_screencast_;
		std::shared_ptr<detail::FrameMetadataStore> metadata_store_;
	};

	explicit VideoSource(VideoSourceOptions options);
	~VideoSource() override = default;

	bool CaptureFrame(const VideoFrame& frame) override;
	uint32_t Width() const override;
	uint32_t Height() const override;
	webrtc::scoped_refptr<InternalSource> Get() const;
	std::shared_ptr<detail::FrameMetadataStore> GetFrameMetadataStore() const;

protected:
	void CaptureRtcFrame(const webrtc::VideoFrame& frame);

private:
	std::shared_ptr<detail::FrameMetadataStore> metadata_store_;
	webrtc::scoped_refptr<InternalSource> source_;
	std::atomic<uint32_t> width_{0};
	std::atomic<uint32_t> height_{0};
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_VIDEO_SOURCE_H_
