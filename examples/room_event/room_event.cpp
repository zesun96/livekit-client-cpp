#include "example_utils.h"

#include "livekit/core/participant/remote_participant_interface.h"
#include "livekit/core/track/remote_track_interface.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

class RoomEvents final : public livekit::core::RoomEventInterface {
public:
	void OnConnected() override {
		connected_.store(true);
		std::cout << "Room connected" << std::endl;
	}

	void OnDisconnected() override { std::cout << "Room disconnected" << std::endl; }

	void OnTrackSubscribed(livekit::core::RemoteTrackInterface* track,
	                       livekit::core::RemoteParticipantInterface* participant) override {
		std::cout << "Subscribed "
		          << (track->Kind() == livekit::core::TrackKind::Audio ? "audio" : "video")
		          << " track " << track->Sid() << " from " << participant->Identity() << std::endl;
	}

	void OnAudioFrame(livekit::core::RemoteTrackInterface*,
	                  livekit::core::RemoteParticipantInterface*,
	                  const livekit::core::AudioFrame&) override {
		if (audio_frames_.fetch_add(1) == 0) {
			std::cout << "Receiving audio frames" << std::endl;
		}
	}

	void OnVideoFrame(livekit::core::RemoteTrackInterface*,
	                  livekit::core::RemoteParticipantInterface*,
	                  const livekit::core::VideoFrame& frame) override {
		if (video_frames_.fetch_add(1) == 0) {
			std::cout << "Receiving " << frame.width << 'x' << frame.height << " video frames"
			          << std::endl;
		}
	}

	void OnDataReceived(const livekit::core::DataReceivedEvent& event) override {
		std::cout << "Data received: topic=" << event.topic << ", bytes=" << event.payload.size()
		          << ", from=" << event.participant_identity << std::endl;
	}

	void OnTextReceived(const livekit::core::TextReceivedEvent& event) override {
		std::cout << "Text received: topic=" << event.topic << ", text=" << event.text
		          << ", from=" << event.participant_identity << std::endl;
	}

	void OnByteReceived(const livekit::core::ByteReceivedEvent& event) override {
		std::cout << "Bytes received: topic=" << event.topic << ", bytes=" << event.data.size()
		          << ", from=" << event.participant_identity << std::endl;
	}

	void OnFileReceived(const livekit::core::FileReceivedEvent& event) override {
		std::cout << "File received: " << event.name << " (" << event.data.size() << " bytes)"
		          << std::endl;
	}

	bool connected() const { return connected_.load(); }

private:
	std::atomic<bool> connected_{false};
	std::atomic<uint64_t> audio_frames_{0};
	std::atomic<uint64_t> video_frames_{0};
};

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
	RoomEvents events;
	room->AddEventListener(&events);
	if (!room->Connect(arguments.url, arguments.token) ||
	    !livekit::examples::WaitUntil([&] { return events.connected(); })) {
		std::cerr << "Failed to connect to LiveKit" << std::endl;
		return 1;
	}

	int listen_seconds = 30;
	if (argc >= 4) {
		try {
			listen_seconds = std::stoi(argv[3]);
		} catch (const std::exception&) {
			std::cerr << "listen-seconds must be a positive integer" << std::endl;
			return 2;
		}
	}
	if (listen_seconds <= 0) {
		std::cerr << "listen-seconds must be a positive integer" << std::endl;
		return 2;
	}
	std::cout << "Listening for room events for " << listen_seconds << " seconds..." << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(listen_seconds));
	room->Disconnect();
	room->RemoveEventListener();
	return 0;
}
