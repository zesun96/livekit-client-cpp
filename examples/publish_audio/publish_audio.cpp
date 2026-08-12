#include "example_utils.h"

#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/audio_source_interface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <thread>
#include <vector>

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

	constexpr uint32_t sample_rate = 48000;
	constexpr uint32_t channels = 1;
	constexpr uint32_t samples_per_frame = sample_rate / 100;
	auto source = livekit::core::CreateAudioSourceUnique({}, sample_rate, channels, 200);
	auto track =
	    room->GetLocalParticipant()->CreateLocalAudioTrackUnique("synthetic-tone", source.get());
	livekit::core::TrackPublishOptions options;
	options.source = livekit::core::TrackSource::Microphone;
	if (!track || !room->GetLocalParticipant()->PublishTrack(track.get(), options)) {
		std::cerr << "Failed to publish audio track" << std::endl;
		return 1;
	}

	std::vector<int16_t> samples(samples_per_frame);
	uint64_t sample_index = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline) {
		for (auto& sample : samples) {
			const double phase = 2.0 * std::numbers::pi * 440.0 * sample_index++ / sample_rate;
			sample = static_cast<int16_t>(std::sin(phase) * 6000.0);
		}
		if (!source->CaptureFrame(samples.data(), sample_rate, channels, samples_per_frame)) {
			std::cerr << "Audio source queue is full" << std::endl;
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	std::cout << "Published a 440 Hz audio tone for 5 seconds" << std::endl;
	if (!room->GetLocalParticipant()->UnpublishTrack(track.get())) {
		std::cerr << "Failed to unpublish audio track" << std::endl;
		return 1;
	}
	track.reset();
	source.reset();
	room->Disconnect();
	return 0;
}
