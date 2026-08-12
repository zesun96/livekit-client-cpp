#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

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
	frame.data.resize(width * height * 3 / 2);
	std::fill(frame.data.begin(), frame.data.begin() + width * height, 64);
	std::fill(frame.data.begin() + width * height, frame.data.end(), 128);
	frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                         std::chrono::steady_clock::now().time_since_epoch())
	                         .count();
	if (!source->CaptureFrame(frame)) {
		std::cerr << "Failed to capture initial video frame" << std::endl;
		return 1;
	}
	auto track =
	    room->GetLocalParticipant()->CreateLocalVideoTrackUnique("synthetic-video", source.get());
	livekit::core::TrackPublishOptions options;
	options.source = livekit::core::TrackSource::Camera;
	options.simulcast = false;
	if (!track || !room->GetLocalParticipant()->PublishTrack(track.get(), options)) {
		std::cerr << "Failed to publish video track" << std::endl;
		return 1;
	}

	for (uint32_t index = 0; index < 150; ++index) {
		const uint8_t luma = static_cast<uint8_t>(32 + index % 180);
		std::fill(frame.data.begin(), frame.data.begin() + width * height, luma);
		std::fill(frame.data.begin() + width * height, frame.data.end(), 128);
		frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                         std::chrono::steady_clock::now().time_since_epoch())
		                         .count();
		if (!source->CaptureFrame(frame)) {
			std::cerr << "Failed to capture video frame" << std::endl;
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}

	std::cout << "Published 640x360 synthetic video for 5 seconds" << std::endl;
	if (!room->GetLocalParticipant()->UnpublishTrack(track.get())) {
		std::cerr << "Failed to unpublish video track" << std::endl;
		return 1;
	}
	track.reset();
	source.reset();
	room->Disconnect();
	return 0;
}
