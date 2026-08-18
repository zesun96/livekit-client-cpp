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

	auto sources = livekit::core::EnumerateScreenCaptureSources();
	for (const auto& candidate : sources) {
		std::cout << (candidate.kind == livekit::core::ScreenCaptureSourceKind::Monitor ? "monitor"
		                                                                                : "window")
		          << " " << candidate.id << " " << candidate.label << " (" << candidate.width << 'x'
		          << candidate.height << ')' << std::endl;
	}
	std::string source_id = argc > 3 ? argv[3] : std::string{};
	if (source_id.empty()) {
		for (const auto& candidate : sources) {
			if (candidate.kind == livekit::core::ScreenCaptureSourceKind::Monitor) {
				source_id = candidate.id;
				break;
			}
		}
	}
	auto source = livekit::core::CreateScreenVideoSourceUnique({std::move(source_id), 15});
	if (!source || !livekit::examples::WaitUntil(
	                   [&] { return source->Width() != 0 && source->Height() != 0; })) {
		std::cerr << "Failed to start the selected screen source" << std::endl;
		return 1;
	}

	auto room = livekit::core::CreateRoomUnique();
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}
	auto track = room->GetLocalParticipant()->CreateLocalVideoTrackUnique("screen", source.get());
	if (!track || !room->GetLocalParticipant()->PublishScreenShareVideoTrack(track.get())) {
		std::cerr << "Failed to publish screen track" << std::endl;
		return 1;
	}

	std::cout << "Publishing " << source->Width() << 'x' << source->Height() << " from "
	          << source->SourceId() << " for 10 seconds" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(10));
	room->GetLocalParticipant()->UnpublishTrack(track.get());
	track.reset();
	source->Stop();
	source.reset();
	room->Disconnect();
	return 0;
}
