#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/rtc_stats.h"
#include "livekit/core/track/video_source_interface.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void FillRgbaFrame(std::vector<std::uint8_t>& data, std::uint32_t width, std::uint32_t height,
                   std::uint8_t phase) {
	data.resize(static_cast<std::size_t>(width) * height * 4U);
	for (std::uint32_t y = 0; y < height; ++y) {
		for (std::uint32_t x = 0; x < width; ++x) {
			const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
			data[offset] = static_cast<std::uint8_t>((x + phase) & 0xffU);
			data[offset + 1U] = static_cast<std::uint8_t>((y + phase * 2U) & 0xffU);
			data[offset + 2U] = static_cast<std::uint8_t>((x + y + phase * 3U) & 0xffU);
			data[offset + 3U] = 255U;
		}
	}
}

} // namespace

int main(int argc, char* argv[]) {
	const auto arguments = livekit::examples::ReadConnectionArguments(argc, argv);
	if (!livekit::examples::ValidateConnectionArguments(arguments, argv[0])) {
		return 2;
	}
	livekit::examples::ClientRuntime runtime;
	if (!runtime.initialized()) {
		std::cerr << "Failed to initialize LiveKit" << std::endl;
		return 1;
	}
	auto room = livekit::core::CreateRoomUnique();
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}

	auto source = livekit::core::CreateVideoSourceUnique();
	constexpr uint32_t width = 640;
	constexpr uint32_t height = 360;
	livekit::core::VideoFrame frame;
	frame.width = width;
	frame.height = height;
	frame.format = livekit::core::VideoBufferType::RGBA;
	FillRgbaFrame(frame.data, width, height, 0);
	frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                         std::chrono::steady_clock::now().time_since_epoch())
	                         .count();
	frame.metadata = livekit::core::VideoFrameMetadata{};
	frame.metadata->user_timestamp_us = static_cast<std::uint64_t>(frame.timestamp_us);
	frame.metadata->frame_id = 1;
	frame.metadata->user_data = std::vector<std::uint8_t>{'d', 'e', 'm', 'o'};
	if (!source->CaptureFrame(frame)) {
		std::cerr << "Failed to capture initial video frame" << std::endl;
		return 1;
	}
	auto track =
	    room->GetLocalParticipant()->CreateLocalVideoTrackUnique("synthetic-video", source.get());
	livekit::core::TrackPublishOptions options;
	options.source = livekit::core::TrackSource::Camera;
	options.simulcast = false;
	options.degradation_preference = livekit::core::VideoDegradationPreference::MaintainFramerate;
	options.frame_metadata_features = livekit::core::FrameMetadataFeatures{true, true, true};
	if (!track || !room->GetLocalParticipant()->PublishTrack(track.get(), options)) {
		std::cerr << "Failed to publish video track" << std::endl;
		return 1;
	}

	livekit::core::RTCStatsMonitor stats_monitor;
	for (uint32_t index = 0; index < 150; ++index) {
		FillRgbaFrame(frame.data, width, height, static_cast<std::uint8_t>(index));
		frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                         std::chrono::steady_clock::now().time_since_epoch())
		                         .count();
		frame.metadata->user_timestamp_us = static_cast<std::uint64_t>(frame.timestamp_us);
		frame.metadata->frame_id = index + 2;
		if (!source->CaptureFrame(frame)) {
			std::cerr << "Failed to capture video frame" << std::endl;
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
		if ((index + 1) % 60 == 0) {
			for (const auto& stats : stats_monitor.Sample(*track).streams) {
				std::cout << "RTC stats: codec=" << stats.codec_mime_type
				          << ", packets=" << stats.packets;
				if (stats.bitrate_bps) {
					std::cout << ", bitrate=" << static_cast<uint64_t>(*stats.bitrate_bps)
					          << " bps";
				}
				std::cout << std::endl;
			}
		}
	}

	std::cout << "Published 640x360 synthetic RGBA video for 5 seconds" << std::endl;
	if (!room->GetLocalParticipant()->UnpublishTrack(track.get())) {
		std::cerr << "Failed to unpublish video track" << std::endl;
		return 1;
	}
	track.reset();
	source.reset();
	room->Disconnect();
	return 0;
}
