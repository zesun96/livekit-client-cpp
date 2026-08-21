#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

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

	livekit::core::CameraCaptureOptions capture_options;
	if (argc > 3) {
		capture_options.device_id = argv[3];
	}
	auto source = livekit::core::CreateCameraVideoSourceUnique(std::move(capture_options));
	if (!source || !livekit::examples::WaitUntil(
	                   [&] { return source->Width() != 0 && source->Height() != 0; })) {
		std::cerr << "Failed to start the selected camera" << std::endl;
		return 1;
	}

	auto room = livekit::core::CreateRoomUnique();
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}
	auto track =
	    room->GetLocalParticipant()->CreateLocalVideoTrackUnique("camera-video", source.get());
	livekit::core::TrackPublishOptions publish_options;
	publish_options.source = livekit::core::TrackSource::Camera;
	if (!track || !room->GetLocalParticipant()->PublishTrack(track.get(), publish_options)) {
		std::cerr << "Failed to publish camera track" << std::endl;
		return 1;
	}

	std::cout << "Publishing " << source->Width() << 'x' << source->Height() << " from "
	          << source->DeviceId() << " for 10 seconds" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(10));
	room->GetLocalParticipant()->UnpublishTrack(track.get());
	track.reset();
	source->Stop();
	source.reset();
	room->Disconnect();
	return 0;
}
