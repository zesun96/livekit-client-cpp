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

#include "api/video/i420_buffer.h"

#include <cstring>

namespace livekit {
namespace core {

RemoteVideoTrack::RemoteVideoTrack(std::string sid, std::string name,
                                   std::unique_ptr<VideoTrack> video_track, FrameCallback callback)
    : RemoteTrack(std::move(sid), std::move(name), TrackKind::Video, std::move(video_track)),
      callback_(std::move(callback)) {
	static_cast<VideoTrack*>(media_track())->AddSink(this);
}

RemoteVideoTrack::~RemoteVideoTrack() { static_cast<VideoTrack*>(media_track())->RemoveSink(this); }

void RemoteVideoTrack::OnFrame(const webrtc::VideoFrame& rtc_frame) {
	auto buffer = rtc_frame.video_frame_buffer()->ToI420();
	if (!buffer) {
		return;
	}
	VideoFrame frame;
	frame.width = static_cast<uint32_t>(buffer->width());
	frame.height = static_cast<uint32_t>(buffer->height());
	frame.timestamp_us = rtc_frame.timestamp_us();
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
	callback_(frame);
}

} // namespace core
} // namespace livekit
