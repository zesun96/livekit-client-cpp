/**
 *
 * Copyright (c) 2024 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "remote_video_track.h"

#include "../detail/frame_metadata.h"
#include "api/media_stream_interface.h"
#include "api/video/i420_buffer.h"
#include "api/video/recordable_encoded_frame.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace livekit {
namespace core {
namespace {

EncodedVideoCodec ConvertCodec(webrtc::VideoCodecType codec) {
	switch (codec) {
	case webrtc::VideoCodecType::kVideoCodecVP8:
		return EncodedVideoCodec::VP8;
	case webrtc::VideoCodecType::kVideoCodecVP9:
		return EncodedVideoCodec::VP9;
	case webrtc::VideoCodecType::kVideoCodecH264:
		return EncodedVideoCodec::H264;
	case webrtc::VideoCodecType::kVideoCodecH265:
		return EncodedVideoCodec::H265;
	case webrtc::VideoCodecType::kVideoCodecAV1:
		return EncodedVideoCodec::AV1;
	default:
		return EncodedVideoCodec::Unknown;
	}
}
} // namespace

class EncodedVideoSubscription final
    : public webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame> {
public:
	EncodedVideoSubscription(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
	                         std::weak_ptr<EncodedVideoStream> stream)
	    : track_(std::move(track)), stream_(std::move(stream)) {}

	~EncodedVideoSubscription() override {
		if (attached_ && source_) {
			source_->RemoveEncodedSink(this);
		}
	}

	bool Attach() {
		source_ = track_ ? track_->GetSource() : nullptr;
		if (source_ == nullptr || !source_->SupportsEncodedOutput()) {
			return false;
		}
		source_->AddEncodedSink(this);
		attached_ = true;
		return true;
	}

	void OnFrame(const webrtc::RecordableEncodedFrame& rtc_frame) override {
		auto stream = stream_.lock();
		if (!stream) {
			return;
		}
		auto buffer = rtc_frame.encoded_buffer();
		if (!buffer || buffer->size() == 0) {
			return;
		}
		EncodedVideoFrame frame;
		frame.data.assign(buffer->data(), buffer->data() + buffer->size());
		frame.codec = ConvertCodec(rtc_frame.codec());
		frame.key_frame = rtc_frame.is_key_frame();
		const auto resolution = rtc_frame.resolution();
		frame.width = resolution.width;
		frame.height = resolution.height;
		frame.timestamp_us = rtc_frame.render_time().ms() * 1000;
		stream->Push(std::move(frame));
	}

private:
	webrtc::scoped_refptr<webrtc::VideoTrackInterface> track_;
	webrtc::VideoTrackSourceInterface* source_ = nullptr;
	std::weak_ptr<EncodedVideoStream> stream_;
	bool attached_ = false;
};

RemoteVideoTrack::RemoteVideoTrack(std::string sid, std::string name,
                                   std::unique_ptr<VideoTrack> video_track, FrameCallback callback,
                                   std::shared_ptr<detail::FrameMetadataStore> metadata_store)
    : RemoteTrack(std::move(sid), std::move(name), TrackKind::Video, std::move(video_track)),
      callback_(std::move(callback)), metadata_store_(std::move(metadata_store)) {
	static_cast<VideoTrack*>(media_track())->AddSink(this);
}

RemoteVideoTrack::~RemoteVideoTrack() {
	static_cast<VideoTrack*>(media_track())->RemoveSink(this);
	CloseStreams();
}

std::shared_ptr<VideoStream> RemoteVideoTrack::CreateVideoStream(MediaStreamOptions options) {
	if (options.capacity == 0) {
		return nullptr;
	}
	auto stream = std::shared_ptr<VideoStream>(new VideoStream(options.capacity));
	std::lock_guard<std::mutex> guard(streams_mutex_);
	streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
	                              [](const auto& weak) { return weak.expired(); }),
	               streams_.end());
	streams_.push_back(stream);
	return stream;
}

std::shared_ptr<EncodedVideoStream>
RemoteVideoTrack::CreateEncodedVideoStream(MediaStreamOptions options) {
	if (options.capacity == 0) {
		return nullptr;
	}
	auto* video_track = static_cast<VideoTrack*>(media_track());
	auto rtc_media_track = video_track->rtc_track();
	auto rtc_video_track = webrtc::scoped_refptr<webrtc::VideoTrackInterface>(
	    static_cast<webrtc::VideoTrackInterface*>(rtc_media_track.get()));
	auto stream = std::shared_ptr<EncodedVideoStream>(new EncodedVideoStream(options.capacity));
	auto subscription =
	    std::make_shared<EncodedVideoSubscription>(std::move(rtc_video_track), stream);
	if (!subscription->Attach()) {
		stream->Close();
		return nullptr;
	}
	stream->SetAttachment(std::move(subscription));
	{
		std::lock_guard<std::mutex> guard(streams_mutex_);
		encoded_streams_.erase(std::remove_if(encoded_streams_.begin(), encoded_streams_.end(),
		                                      [](const auto& weak) { return weak.expired(); }),
		                       encoded_streams_.end());
		encoded_streams_.push_back(stream);
	}
	return stream;
}

std::shared_ptr<detail::FrameMetadataStore> RemoteVideoTrack::GetFrameMetadataStore() const {
	return metadata_store_;
}

void RemoteVideoTrack::OnFrame(const webrtc::VideoFrame& rtc_frame) {
	auto buffer = rtc_frame.video_frame_buffer()->ToI420();
	if (!buffer) {
		return;
	}
	VideoFrame frame;
	frame.width = static_cast<uint32_t>(buffer->width());
	frame.height = static_cast<uint32_t>(buffer->height());
	frame.timestamp_us = rtc_frame.timestamp_us();
	if (metadata_store_) {
		frame.metadata = metadata_store_->Find(rtc_frame.rtp_timestamp());
	}
	const std::size_t y_size = static_cast<std::size_t>(frame.width) * frame.height;
	const std::size_t chroma_width = (frame.width + 1) / 2;
	const std::size_t chroma_height = (frame.height + 1) / 2;
	const std::size_t chroma_size = chroma_width * chroma_height;
	frame.data.resize(y_size + chroma_size * 2);
	frame.format = VideoBufferType::I420;
	frame.planes = {{0, y_size, frame.width},
	                {y_size, chroma_size, static_cast<std::uint32_t>(chroma_width)},
	                {y_size + chroma_size, chroma_size, static_cast<std::uint32_t>(chroma_width)}};
	for (uint32_t row = 0; row < frame.height; ++row) {
		std::memcpy(frame.data.data() + static_cast<std::size_t>(row) * frame.width,
		            buffer->DataY() + static_cast<std::size_t>(row) * buffer->StrideY(),
		            frame.width);
	}
	for (std::size_t row = 0; row < chroma_height; ++row) {
		std::memcpy(frame.data.data() + y_size + row * chroma_width,
		            buffer->DataU() + row * buffer->StrideU(), chroma_width);
		std::memcpy(frame.data.data() + y_size + chroma_size + row * chroma_width,
		            buffer->DataV() + row * buffer->StrideV(), chroma_width);
	}
	std::vector<std::shared_ptr<VideoStream>> streams;
	{
		std::lock_guard<std::mutex> guard(streams_mutex_);
		for (auto it = streams_.begin(); it != streams_.end();) {
			if (auto stream = it->lock()) {
				streams.push_back(std::move(stream));
				++it;
			} else {
				it = streams_.erase(it);
			}
		}
	}
	for (const auto& stream : streams) {
		stream->Push(frame);
	}
	if (callback_) {
		callback_(frame);
	}
}

void RemoteVideoTrack::CloseStreams() {
	std::vector<std::shared_ptr<VideoStream>> streams;
	std::vector<std::shared_ptr<EncodedVideoStream>> encoded_streams;
	{
		std::lock_guard<std::mutex> guard(streams_mutex_);
		for (const auto& weak : streams_) {
			if (auto stream = weak.lock()) {
				streams.push_back(std::move(stream));
			}
		}
		streams_.clear();
		for (const auto& weak : encoded_streams_) {
			if (auto stream = weak.lock()) {
				encoded_streams.push_back(std::move(stream));
			}
		}
		encoded_streams_.clear();
	}
	for (const auto& stream : streams) {
		stream->Close();
	}
	for (const auto& stream : encoded_streams) {
		stream->Close();
	}
}

} // namespace core
} // namespace livekit
