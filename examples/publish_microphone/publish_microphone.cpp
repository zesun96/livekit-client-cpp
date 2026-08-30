#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/audio_source_interface.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
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

	livekit::core::MicrophoneCaptureOptions capture_options;
	capture_options.processing.echo_cancellation = true;
	capture_options.processing.auto_gain_control = true;
	capture_options.processing.noise_suppression = true;
	if (argc > 3) {
		capture_options.device_id = argv[3];
	}
	auto source = livekit::core::CreateMicrophoneAudioSourceUnique(std::move(capture_options));
	if (!source) {
		std::cerr << "Failed to start the selected microphone" << std::endl;
		return 1;
	}
	if (argc > 4) {
		try {
			std::size_t parsed = 0;
			const std::string value = argv[4];
			const float volume = std::stof(value, &parsed);
			if (parsed != value.size() || !source->SetVolume(volume)) {
				std::cerr << "Microphone volume must be a number between 0 and 1" << std::endl;
				return 2;
			}
		} catch (const std::exception&) {
			std::cerr << "Microphone volume must be a number between 0 and 1" << std::endl;
			return 2;
		}
	}

	auto room = livekit::core::CreateRoomUnique();
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return room->IsConnected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}
	auto track =
	    room->GetLocalParticipant()->CreateLocalAudioTrackUnique("microphone", source.get());
	livekit::core::TrackPublishOptions publish_options;
	publish_options.source = livekit::core::TrackSource::Microphone;
	publish_options.preconnect_buffer = std::getenv("LIVEKIT_PRECONNECT_BUFFER") != nullptr;
	if (!track || !room->GetLocalParticipant()->PublishTrack(track.get(), publish_options)) {
		std::cerr << "Failed to publish microphone track" << std::endl;
		return 1;
	}

	std::cout << "Publishing microphone "
	          << (source->DeviceId().empty() ? "[system default]" : source->DeviceId())
	          << " at volume " << source->Volume() << " for 10 seconds" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(10));
	room->GetLocalParticipant()->UnpublishTrack(track.get());
	track.reset();
	source->Stop();
	source.reset();
	room->Disconnect();
	return 0;
}
