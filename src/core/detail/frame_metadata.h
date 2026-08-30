#pragma once

#include "livekit/core/option/media_option.h"
#include "livekit/core/track/video_frame.h"

#include "api/frame_transformer_interface.h"
#include "api/scoped_refptr.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace livekit::core::detail {

class FrameMetadataStore final {
public:
	void Store(std::uint32_t rtp_timestamp, VideoFrameMetadata metadata,
	           std::optional<std::int64_t> capture_timestamp_us = std::nullopt);
	std::optional<VideoFrameMetadata> Find(std::uint32_t rtp_timestamp) const;
	std::optional<VideoFrameMetadata>
	FindByCaptureTimestamp(std::int64_t capture_timestamp_us) const;

private:
	static constexpr std::size_t kCapacity = 256;
	mutable std::mutex mutex_;
	std::map<std::uint32_t, VideoFrameMetadata> entries_;
	std::map<std::int64_t, VideoFrameMetadata> capture_entries_;
	std::vector<std::uint32_t> insertion_order_;
	std::vector<std::int64_t> capture_insertion_order_;
};

std::uint32_t VideoRtpTimestampFromMicros(std::int64_t timestamp_us) noexcept;
bool IsValidVideoFrameMetadata(const std::optional<VideoFrameMetadata>& metadata) noexcept;

struct ExtractedFrameMetadata {
	std::vector<std::uint8_t> payload;
	std::optional<VideoFrameMetadata> metadata;
};

std::vector<std::uint8_t> AppendFrameMetadataTrailer(const std::vector<std::uint8_t>& payload,
                                                     const VideoFrameMetadata& metadata,
                                                     const FrameMetadataFeatures& features);
ExtractedFrameMetadata ExtractFrameMetadataTrailer(const std::vector<std::uint8_t>& data);

webrtc::scoped_refptr<webrtc::FrameTransformerInterface> CreateFrameMetadataTransformer(
    bool sender, std::shared_ptr<FrameMetadataStore> store, FrameMetadataFeatures features = {},
    webrtc::scoped_refptr<webrtc::FrameTransformerInterface> chained_transformer = nullptr);

} // namespace livekit::core::detail
