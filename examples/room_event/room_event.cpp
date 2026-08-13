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

	void OnDisconnected(livekit::core::DisconnectReason reason) override {
		std::cout << "Room disconnected, reason=" << static_cast<int>(reason) << std::endl;
	}
	void OnReconnecting() override { std::cout << "Room reconnecting" << std::endl; }
	void OnReconnected() override { std::cout << "Room reconnected" << std::endl; }
	void OnConnectionStateChanged(livekit::core::RoomState state) override {
		std::cout << "Connection state changed: " << static_cast<int>(state) << std::endl;
	}
	void OnRecordingStatusChanged(bool recording) override {
		std::cout << "Recording status changed: " << (recording ? "active" : "inactive")
		          << std::endl;
	}

	void OnTrackSubscribed(livekit::core::RemoteTrackInterface* track,
	                       livekit::core::RemoteParticipantInterface* participant) override {
		std::cout << "Subscribed "
		          << (track->Kind() == livekit::core::TrackKind::Audio ? "audio" : "video")
		          << " track " << track->Sid() << " from " << participant->Identity() << std::endl;
	}

	void
	OnParticipantPermissionsChanged(const livekit::core::ParticipantPermissions& previous,
	                                livekit::core::ParticipantInterface* participant) override {
		const auto current = participant->Permissions();
		std::cout << "Participant permissions changed: " << participant->Identity()
		          << ", publish=" << previous.can_publish << "->" << current.can_publish
		          << ", subscribe=" << previous.can_subscribe << "->" << current.can_subscribe
		          << std::endl;
	}

	void OnTrackSubscriptionFailed(const std::string& track_sid,
	                               livekit::core::RemoteParticipantInterface* participant,
	                               livekit::core::SubscriptionError error) override {
		std::cout << "Subscription failed for track " << track_sid << " from "
		          << participant->Identity() << ", error=" << static_cast<int>(error) << std::endl;
	}

	void OnTrackUnsubscribed(livekit::core::RemoteTrackInterface* track,
	                         livekit::core::TrackPublicationInterface*,
	                         livekit::core::RemoteParticipantInterface* participant) override {
		std::cout << "Unsubscribed track " << track->Sid() << " from " << participant->Identity()
		          << std::endl;
	}

	void OnTrackStreamStateChanged(livekit::core::TrackPublicationInterface* publication,
	                               livekit::core::RemoteParticipantInterface* participant,
	                               livekit::core::TrackStreamState state) override {
		std::cout << "Track " << publication->Sid() << " from " << participant->Identity()
		          << " stream state=" << static_cast<int>(state) << std::endl;
	}

	void OnTrackSubscriptionStatusChanged(livekit::core::TrackPublicationInterface* publication,
	                                      livekit::core::RemoteParticipantInterface* participant,
	                                      livekit::core::TrackSubscriptionStatus status) override {
		std::cout << "Track " << publication->Sid() << " from " << participant->Identity()
		          << " subscription status=" << static_cast<int>(status) << std::endl;
	}

	void
	OnTrackSubscriptionPermissionChanged(livekit::core::TrackPublicationInterface* track,
	                                     livekit::core::RemoteParticipantInterface* participant,
	                                     bool allowed) override {
		std::cout << "Subscription permission " << (allowed ? "granted" : "revoked")
		          << " for track " << track->Sid() << " from " << participant->Identity()
		          << std::endl;
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

	void OnDataChannelBufferStatusChanged(
	    const livekit::core::DataChannelBufferStatus& status) override {
		std::cout << (status.reliable ? "Reliable" : "Lossy")
		          << " data channel backpressure=" << status.backpressured
		          << ", buffered=" << status.buffered_amount << std::endl;
	}

	void OnSipDtmfReceived(const livekit::core::SipDtmfEvent& event) override {
		std::cout << "SIP DTMF received: code=" << event.code << ", digit=" << event.digit
		          << ", from=" << event.participant_identity << std::endl;
	}

	void OnChatMessageReceived(const livekit::core::ChatMessage& message) override {
		std::cout << "Chat message received: id=" << message.id
		          << ", edited=" << message.edit_timestamp.has_value()
		          << ", text=" << message.message << ", from=" << message.participant_identity
		          << std::endl;
	}

	void OnTranscriptionReceived(
	    const livekit::core::TranscriptionReceivedEvent& transcription) override {
		std::cout << "Transcription received: participant="
		          << transcription.transcribed_participant_identity
		          << ", track=" << transcription.track_id << std::endl;
		for (const auto& segment : transcription.segments) {
			std::cout << "  id=" << segment.id << ", final=" << segment.final
			          << ", language=" << segment.language << ", text=" << segment.text
			          << std::endl;
		}
	}

	void OnMetricsReceived(const livekit::core::MetricsReceivedEvent& metrics) override {
		std::cout << "Metrics received: series=" << metrics.time_series.size()
		          << ", events=" << metrics.events.size()
		          << ", from=" << metrics.participant_identity << std::endl;
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
	room->RegisterTextStreamHandler(
	    "incremental-text-transfer", [](const livekit::core::TextStreamEvent& event) {
		    std::cout << "Incremental text stream " << event.info.stream_id
		              << ": state=" << static_cast<int>(event.type)
		              << ", chunk-bytes=" << event.content.size() << std::endl;
	    });
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
	room->UnregisterTextStreamHandler("incremental-text-transfer");
	room->RemoveEventListener();
	return 0;
}
