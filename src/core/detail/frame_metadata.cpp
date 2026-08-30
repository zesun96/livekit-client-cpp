#include "frame_metadata.h"

#include "logging.h"

#include "api/make_ref_counted.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace livekit::core::detail {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'L', 'K', 'T', 'S'};
constexpr std::uint8_t kTimestampTag = 0x01;
constexpr std::uint8_t kFrameIdTag = 0x02;
constexpr std::uint8_t kUserDataTag = 0x03;
constexpr std::size_t kEnvelopeSize = 5;

void AppendByte(std::vector<std::uint8_t>& out, std::uint8_t value) {
	out.push_back(static_cast<std::uint8_t>(value ^ 0xffU));
}

template <typename T> void AppendBigEndian(std::vector<std::uint8_t>& out, T value) {
	for (std::size_t index = sizeof(T); index > 0; --index) {
		AppendByte(out, static_cast<std::uint8_t>(value >> ((index - 1) * 8)));
	}
}

template <typename T> T ReadBigEndian(const std::uint8_t* data, std::size_t size) {
	T value = 0;
	for (std::size_t index = 0; index < size; ++index) {
		value = static_cast<T>((value << 8) | static_cast<T>(data[index] ^ 0xffU));
	}
	return value;
}

class FrameMetadataTransformer;

class ChainedCallback : public webrtc::TransformedFrameCallback {
public:
	explicit ChainedCallback(FrameMetadataTransformer* owner) : owner_(owner) {}
	void OnTransformedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) override;

private:
	FrameMetadataTransformer* owner_;
};

class FrameMetadataTransformer : public webrtc::FrameTransformerInterface {
public:
	FrameMetadataTransformer(
	    bool sender, std::shared_ptr<FrameMetadataStore> store, FrameMetadataFeatures features,
	    webrtc::scoped_refptr<webrtc::FrameTransformerInterface> chained_transformer)
	    : sender_(sender), store_(std::move(store)), features_(features),
	      chained_transformer_(std::move(chained_transformer)),
	      chained_callback_(webrtc::make_ref_counted<ChainedCallback>(this)) {}

	~FrameMetadataTransformer() override {
		if (chained_transformer_) {
			chained_transformer_->UnregisterTransformedFrameCallback();
			std::vector<std::uint32_t> ssrcs;
			{
				std::lock_guard<std::mutex> guard(callback_mutex_);
				for (const auto& [ssrc, callback] : sink_callbacks_) {
					(void)callback;
					ssrcs.push_back(ssrc);
				}
			}
			for (const auto ssrc : ssrcs) {
				chained_transformer_->UnregisterTransformedFrameSinkCallback(ssrc);
			}
		}
	}

	void Transform(std::unique_ptr<webrtc::TransformableFrameInterface> frame) override {
		if (!frame) {
			return;
		}
		if (!sender_) {
			Process(std::move(frame));
			return;
		}
		if (chained_transformer_) {
			chained_transformer_->Transform(std::move(frame));
		} else {
			Process(std::move(frame));
		}
	}

	void RegisterTransformedFrameCallback(
	    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback) override {
		{
			std::lock_guard<std::mutex> guard(callback_mutex_);
			callback_ = std::move(callback);
		}
		if (chained_transformer_) {
			chained_transformer_->RegisterTransformedFrameCallback(chained_callback_);
		}
	}

	void RegisterTransformedFrameSinkCallback(
	    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback,
	    std::uint32_t ssrc) override {
		{
			std::lock_guard<std::mutex> guard(callback_mutex_);
			sink_callbacks_[ssrc] = std::move(callback);
		}
		if (chained_transformer_) {
			chained_transformer_->RegisterTransformedFrameSinkCallback(chained_callback_, ssrc);
		}
	}

	void UnregisterTransformedFrameCallback() override {
		{
			std::lock_guard<std::mutex> guard(callback_mutex_);
			callback_ = nullptr;
		}
		if (chained_transformer_) {
			chained_transformer_->UnregisterTransformedFrameCallback();
		}
	}

	void UnregisterTransformedFrameSinkCallback(std::uint32_t ssrc) override {
		{
			std::lock_guard<std::mutex> guard(callback_mutex_);
			sink_callbacks_.erase(ssrc);
		}
		if (chained_transformer_) {
			chained_transformer_->UnregisterTransformedFrameSinkCallback(ssrc);
		}
	}

	void OnChainedFrame(std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
		if (sender_) {
			Process(std::move(frame));
		} else {
			Dispatch(std::move(frame));
		}
	}

private:
	void Process(std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
		const bool is_video =
		    dynamic_cast<webrtc::TransformableVideoFrameInterface*>(frame.get()) != nullptr;
		if (is_video && store_) {
			const auto data = frame->GetData();
			std::vector<std::uint8_t> bytes(data.begin(), data.end());
			if (sender_) {
				auto metadata = store_->Find(frame->GetTimestamp());
				if (!metadata) {
					if (const auto capture_time = frame->CaptureTime()) {
						metadata = store_->FindByCaptureTimestamp(capture_time->us());
					}
				}
				if (metadata) {
					auto with_trailer = AppendFrameMetadataTrailer(bytes, *metadata, features_);
					if (with_trailer.size() != bytes.size()) {
						frame->SetData(with_trailer);
						LKC_LOG_TRACE << "appended video frame metadata: rtp_timestamp="
						              << frame->GetTimestamp();
					}
				}
			} else {
				auto extracted = ExtractFrameMetadataTrailer(bytes);
				if (extracted.payload.size() != bytes.size()) {
					frame->SetData(extracted.payload);
				}
				if (extracted.metadata) {
					store_->Store(frame->GetTimestamp(), std::move(*extracted.metadata));
					LKC_LOG_TRACE << "extracted video frame metadata: rtp_timestamp="
					              << frame->GetTimestamp();
				}
			}
		}

		if (!sender_ && chained_transformer_) {
			chained_transformer_->Transform(std::move(frame));
		} else {
			Dispatch(std::move(frame));
		}
	}

	void Dispatch(std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
		webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback;
		{
			std::lock_guard<std::mutex> guard(callback_mutex_);
			callback = callback_;
			if (!callback) {
				const auto found = sink_callbacks_.find(frame->GetSsrc());
				if (found != sink_callbacks_.end()) {
					callback = found->second;
				}
			}
		}
		if (callback) {
			callback->OnTransformedFrame(std::move(frame));
		}
	}

	bool sender_;
	std::shared_ptr<FrameMetadataStore> store_;
	FrameMetadataFeatures features_;
	webrtc::scoped_refptr<webrtc::FrameTransformerInterface> chained_transformer_;
	webrtc::scoped_refptr<ChainedCallback> chained_callback_;
	std::mutex callback_mutex_;
	webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback_;
	std::map<std::uint32_t, webrtc::scoped_refptr<webrtc::TransformedFrameCallback>>
	    sink_callbacks_;
};

void ChainedCallback::OnTransformedFrame(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
	owner_->OnChainedFrame(std::move(frame));
}

} // namespace

void FrameMetadataStore::Store(std::uint32_t rtp_timestamp, VideoFrameMetadata metadata,
                               std::optional<std::int64_t> capture_timestamp_us) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (!entries_.contains(rtp_timestamp)) {
		insertion_order_.push_back(rtp_timestamp);
	}
	entries_[rtp_timestamp] = metadata;
	if (capture_timestamp_us) {
		// EncodedImage carries capture time in milliseconds, even when the input VideoFrame used a
		// microsecond timestamp. Normalize the input key to that same precision.
		const auto normalized_capture_timestamp_us = *capture_timestamp_us / 1000 * 1000;
		if (!capture_entries_.contains(normalized_capture_timestamp_us)) {
			capture_insertion_order_.push_back(normalized_capture_timestamp_us);
		}
		capture_entries_[normalized_capture_timestamp_us] = std::move(metadata);
	}
	while (insertion_order_.size() > kCapacity) {
		const auto oldest = insertion_order_.front();
		insertion_order_.erase(insertion_order_.begin());
		entries_.erase(oldest);
	}
	while (capture_insertion_order_.size() > kCapacity) {
		const auto oldest = capture_insertion_order_.front();
		capture_insertion_order_.erase(capture_insertion_order_.begin());
		capture_entries_.erase(oldest);
	}
}

std::optional<VideoFrameMetadata>
FrameMetadataStore::FindByCaptureTimestamp(std::int64_t capture_timestamp_us) const {
	std::lock_guard<std::mutex> guard(mutex_);
	const auto normalized_capture_timestamp_us = capture_timestamp_us / 1000 * 1000;
	const auto found = capture_entries_.find(normalized_capture_timestamp_us);
	return found == capture_entries_.end() ? std::nullopt
	                                       : std::optional<VideoFrameMetadata>(found->second);
}

std::optional<VideoFrameMetadata> FrameMetadataStore::Find(std::uint32_t rtp_timestamp) const {
	std::lock_guard<std::mutex> guard(mutex_);
	const auto found = entries_.find(rtp_timestamp);
	return found == entries_.end() ? std::nullopt
	                               : std::optional<VideoFrameMetadata>(found->second);
}

std::uint32_t VideoRtpTimestampFromMicros(std::int64_t timestamp_us) noexcept {
	const auto value = static_cast<std::uint64_t>(timestamp_us) * 90ULL / 1000ULL;
	return static_cast<std::uint32_t>(value & std::numeric_limits<std::uint32_t>::max());
}

bool IsValidVideoFrameMetadata(const std::optional<VideoFrameMetadata>& metadata) noexcept {
	return !metadata || !metadata->user_data ||
	       metadata->user_data->size() <= kMaxVideoFrameMetadataUserDataSize;
}

std::vector<std::uint8_t> AppendFrameMetadataTrailer(const std::vector<std::uint8_t>& payload,
                                                     const VideoFrameMetadata& metadata,
                                                     const FrameMetadataFeatures& features) {
	std::vector<std::uint8_t> trailer;
	if (features.user_timestamp && metadata.user_timestamp_us) {
		AppendByte(trailer, kTimestampTag);
		AppendByte(trailer, sizeof(std::uint64_t));
		AppendBigEndian(trailer, *metadata.user_timestamp_us);
	}
	if (features.frame_id && metadata.frame_id) {
		AppendByte(trailer, kFrameIdTag);
		AppendByte(trailer, sizeof(std::uint32_t));
		AppendBigEndian(trailer, *metadata.frame_id);
	}
	if (features.user_data && metadata.user_data &&
	    metadata.user_data->size() <= kMaxVideoFrameMetadataUserDataSize) {
		AppendByte(trailer, kUserDataTag);
		AppendByte(trailer, static_cast<std::uint8_t>(metadata.user_data->size()));
		for (const auto byte : *metadata.user_data) {
			AppendByte(trailer, byte);
		}
	}
	if (trailer.empty() || trailer.size() + kEnvelopeSize > 255) {
		return payload;
	}

	std::vector<std::uint8_t> result;
	result.reserve(payload.size() + trailer.size() + kEnvelopeSize);
	result.insert(result.end(), payload.begin(), payload.end());
	result.insert(result.end(), trailer.begin(), trailer.end());
	AppendByte(result, static_cast<std::uint8_t>(trailer.size() + kEnvelopeSize));
	result.insert(result.end(), kMagic.begin(), kMagic.end());
	return result;
}

ExtractedFrameMetadata ExtractFrameMetadataTrailer(const std::vector<std::uint8_t>& data) {
	ExtractedFrameMetadata result{data, std::nullopt};
	if (data.size() < kEnvelopeSize ||
	    !std::equal(kMagic.begin(), kMagic.end(), data.end() - kMagic.size())) {
		return result;
	}
	const std::size_t trailer_length = data[data.size() - kEnvelopeSize] ^ 0xffU;
	if (trailer_length < kEnvelopeSize || trailer_length > data.size()) {
		return result;
	}
	const std::size_t trailer_start = data.size() - trailer_length;
	const std::size_t trailer_end = data.size() - kEnvelopeSize;
	std::size_t offset = trailer_start;
	VideoFrameMetadata metadata;
	bool found_any = false;
	while (offset + 2 <= trailer_end) {
		const auto tag = data[offset++] ^ 0xffU;
		const std::size_t length = data[offset++] ^ 0xffU;
		if (offset + length > trailer_end) {
			return result;
		}
		if (tag == kTimestampTag && length == sizeof(std::uint64_t)) {
			metadata.user_timestamp_us = ReadBigEndian<std::uint64_t>(data.data() + offset, length);
			found_any = true;
		} else if (tag == kFrameIdTag && length == sizeof(std::uint32_t)) {
			metadata.frame_id = ReadBigEndian<std::uint32_t>(data.data() + offset, length);
			found_any = true;
		} else if (tag == kUserDataTag) {
			std::vector<std::uint8_t> user_data(length);
			for (std::size_t index = 0; index < length; ++index) {
				user_data[index] = data[offset + index] ^ 0xffU;
			}
			metadata.user_data = std::move(user_data);
			found_any = true;
		}
		offset += length;
	}
	if (!found_any || offset != trailer_end) {
		return result;
	}
	result.payload.assign(data.begin(), data.begin() + trailer_start);
	result.metadata = std::move(metadata);
	return result;
}

webrtc::scoped_refptr<webrtc::FrameTransformerInterface> CreateFrameMetadataTransformer(
    bool sender, std::shared_ptr<FrameMetadataStore> store, FrameMetadataFeatures features,
    webrtc::scoped_refptr<webrtc::FrameTransformerInterface> chained_transformer) {
	return webrtc::make_ref_counted<FrameMetadataTransformer>(sender, std::move(store), features,
	                                                          std::move(chained_transformer));
}

} // namespace livekit::core::detail
