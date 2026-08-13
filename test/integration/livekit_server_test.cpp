#include "../../src/core/room.h"
#include "livekit/core/livekit_client.h"
#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/participant/remote_participant_interface.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/remote_track_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace livekit::core {
namespace {

class ClientRuntime {
public:
	ClientRuntime() : initialized_(Init()) {}
	~ClientRuntime() {
		if (initialized_) {
			Destroy();
		}
	}

	bool initialized() const { return initialized_; }

private:
	bool initialized_;
};

bool WaitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	do {
		if (predicate()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	} while (std::chrono::steady_clock::now() < deadline);
	return predicate();
}

std::string PublicationSummary(ParticipantInterface* participant) {
	if (participant == nullptr) {
		return "participant is null";
	}
	std::ostringstream summary;
	for (auto* publication : participant->GetTrackPublications()) {
		summary << "[sid=" << publication->Sid() << ", name=" << publication->Name()
		        << ", kind=" << static_cast<int>(publication->Kind())
		        << ", source=" << static_cast<int>(publication->Source())
		        << ", muted=" << publication->IsMuted() << "]";
	}
	return summary.str();
}

class MediaEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnReconnecting() override { reconnecting_.fetch_add(1); }
	void OnReconnected() override { reconnected_.fetch_add(1); }

	void OnTrackSubscribed(RemoteTrackInterface* track, RemoteParticipantInterface*) override {
		if (track->Kind() == TrackKind::Audio) {
			audio_subscribed_.store(true);
			audio_subscribed_count_.fetch_add(1);
		} else if (track->Kind() == TrackKind::Video) {
			video_subscribed_.store(true);
		}
	}

	void OnTrackUnsubscribed(RemoteTrackInterface* track, TrackPublicationInterface*,
	                         RemoteParticipantInterface*) override {
		if (track != nullptr) {
			if (track->Kind() == TrackKind::Audio) {
				audio_unsubscribed_.fetch_add(1);
			} else if (track->Kind() == TrackKind::Video) {
				video_unsubscribed_.fetch_add(1);
			}
		}
	}

	void OnTrackSubscriptionStatusChanged(TrackPublicationInterface* publication,
	                                      RemoteParticipantInterface*,
	                                      TrackSubscriptionStatus status) override {
		std::lock_guard<std::mutex> guard(lock_);
		subscription_status_sid_ = publication != nullptr ? publication->Sid() : "";
		subscription_status_ = status;
		++subscription_status_count_;
	}

	void OnTrackSubscriptionPermissionChanged(TrackPublicationInterface* track,
	                                          RemoteParticipantInterface*, bool allowed) override {
		std::lock_guard<std::mutex> guard(lock_);
		permission_track_sid_ = track != nullptr ? track->Sid() : "";
		permission_allowed_ = allowed;
		++permission_change_count_;
	}

	void OnLocalTrackPublished(TrackPublicationInterface*, ParticipantInterface*) override {
		local_tracks_published_.fetch_add(1);
	}

	void OnLocalTrackUnpublished(TrackPublicationInterface*, ParticipantInterface*) override {
		local_tracks_unpublished_.fetch_add(1);
	}

	void OnAudioFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                  const AudioFrame& frame) override {
		if (!frame.data.empty() && frame.sample_rate > 0 && frame.num_channels > 0) {
			audio_frames_.fetch_add(1);
		}
	}

	void OnVideoFrame(RemoteTrackInterface*, RemoteParticipantInterface*,
	                  const VideoFrame& frame) override {
		last_video_width_.store(frame.width);
		last_video_height_.store(frame.height);
		const auto expected_size = static_cast<std::size_t>(frame.width) * frame.height * 3 / 2;
		if (frame.width > 0 && frame.height > 0 && frame.data.size() == expected_size) {
			video_frames_.fetch_add(1);
		}
	}

	void OnDataReceived(const DataReceivedEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		data_topic_ = event.topic;
		data_ = event.payload;
		data_reliable_ = event.reliable;
	}

	void OnFileReceived(const FileReceivedEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		file_name_ = event.name;
		file_mime_type_ = event.mime_type;
		file_topic_ = event.topic;
		file_data_ = event.data;
	}

	void OnTextReceived(const TextReceivedEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		text_topic_ = event.topic;
		text_ = event.text;
	}

	void OnByteReceived(const ByteReceivedEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		byte_topic_ = event.topic;
		byte_data_ = event.data;
	}

	void OnSipDtmfReceived(const SipDtmfEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		dtmf_code_ = event.code;
		dtmf_digit_ = event.digit;
		dtmf_identity_ = event.participant_identity;
	}

	void OnChatMessageReceived(const ChatMessage& message) override {
		std::lock_guard<std::mutex> guard(lock_);
		chat_message_ = message;
	}

	void OnLocalTrackSubscribed(TrackPublicationInterface* publication,
	                            ParticipantInterface*) override {
		std::lock_guard<std::mutex> guard(lock_);
		local_track_subscribed_sid_ = publication != nullptr ? publication->Sid() : "";
	}

	bool audio_received() const { return audio_subscribed_.load() && audio_frames_.load() >= 5; }
	bool video_received() const { return video_subscribed_.load() && video_frames_.load() >= 3; }
	bool video_subscribed() const { return video_subscribed_.load(); }
	bool reconnecting() const { return reconnecting_.load() > 0; }
	bool reconnected() const { return reconnected_.load() > 0; }
	uint64_t reconnecting_count() const { return reconnecting_.load(); }
	uint64_t reconnected_count() const { return reconnected_.load(); }
	uint64_t audio_frame_count() const { return audio_frames_.load(); }
	uint64_t audio_subscribed_count() const { return audio_subscribed_count_.load(); }
	uint64_t audio_unsubscribed_count() const { return audio_unsubscribed_.load(); }
	uint64_t video_frame_count() const { return video_frames_.load(); }
	uint64_t video_unsubscribed_count() const { return video_unsubscribed_.load(); }
	uint32_t last_video_width() const { return last_video_width_.load(); }
	uint32_t last_video_height() const { return last_video_height_.load(); }
	uint64_t local_tracks_published() const { return local_tracks_published_.load(); }
	uint64_t local_tracks_unpublished() const { return local_tracks_unpublished_.load(); }
	bool received_data(const std::string& topic, const std::vector<uint8_t>& expected,
	                   bool reliable) {
		std::lock_guard<std::mutex> guard(lock_);
		return data_topic_ == topic && data_ == expected && data_reliable_ == reliable;
	}
	bool received_file(const std::string& name, const std::string& mime_type,
	                   const std::string& topic, const std::vector<uint8_t>& expected) {
		std::lock_guard<std::mutex> guard(lock_);
		return file_name_ == name && file_mime_type_ == mime_type && file_topic_ == topic &&
		       file_data_ == expected;
	}
	bool received_text(const std::string& topic, const std::string& text) {
		std::lock_guard<std::mutex> guard(lock_);
		return text_topic_ == topic && text_ == text;
	}
	bool received_bytes(const std::string& topic, const std::vector<uint8_t>& data) {
		std::lock_guard<std::mutex> guard(lock_);
		return byte_topic_ == topic && byte_data_ == data;
	}
	bool received_dtmf(uint32_t code, const std::string& digit, const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return dtmf_code_ == code && dtmf_digit_ == digit && dtmf_identity_ == identity;
	}
	bool received_chat(const std::string& id, const std::string& text, bool edited,
	                   const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return chat_message_.id == id && chat_message_.message == text &&
		       chat_message_.edit_timestamp.has_value() == edited &&
		       chat_message_.participant_identity == identity;
	}
	bool local_track_subscribed(const std::string& sid) {
		std::lock_guard<std::mutex> guard(lock_);
		return local_track_subscribed_sid_ == sid;
	}
	bool permission_changed(const std::string& track_sid, bool allowed,
	                        uint64_t minimum_count = 1) {
		std::lock_guard<std::mutex> guard(lock_);
		return permission_track_sid_ == track_sid && permission_allowed_ == allowed &&
		       permission_change_count_ >= minimum_count;
	}
	bool subscription_status(const std::string& track_sid, TrackSubscriptionStatus status,
	                         uint64_t minimum_count = 1) {
		std::lock_guard<std::mutex> guard(lock_);
		return subscription_status_sid_ == track_sid && subscription_status_ == status &&
		       subscription_status_count_ >= minimum_count;
	}

private:
	std::atomic<bool> audio_subscribed_{false};
	std::atomic<bool> video_subscribed_{false};
	std::atomic<uint64_t> reconnecting_{0};
	std::atomic<uint64_t> reconnected_{0};
	std::atomic<uint64_t> audio_frames_{0};
	std::atomic<uint64_t> audio_subscribed_count_{0};
	std::atomic<uint64_t> audio_unsubscribed_{0};
	std::atomic<uint64_t> video_frames_{0};
	std::atomic<uint64_t> video_unsubscribed_{0};
	std::atomic<uint32_t> last_video_width_{0};
	std::atomic<uint32_t> last_video_height_{0};
	std::atomic<uint64_t> local_tracks_published_{0};
	std::atomic<uint64_t> local_tracks_unpublished_{0};
	std::mutex lock_;
	std::string data_topic_;
	std::vector<uint8_t> data_;
	bool data_reliable_ = false;
	std::string file_name_;
	std::string file_mime_type_;
	std::string file_topic_;
	std::vector<uint8_t> file_data_;
	std::string text_topic_;
	std::string text_;
	std::string byte_topic_;
	std::vector<uint8_t> byte_data_;
	uint32_t dtmf_code_ = 0;
	std::string dtmf_digit_;
	std::string dtmf_identity_;
	ChatMessage chat_message_;
	std::string local_track_subscribed_sid_;
	std::string permission_track_sid_;
	bool permission_allowed_ = true;
	uint64_t permission_change_count_ = 0;
	std::string subscription_status_sid_;
	TrackSubscriptionStatus subscription_status_ = TrackSubscriptionStatus::Unsubscribed;
	uint64_t subscription_status_count_ = 0;
};

class ParticipantEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnParticipantConnected(RemoteParticipantInterface* participant) override {
		std::lock_guard<std::mutex> guard(lock_);
		connected_identity_ = participant->Identity();
	}
	void OnParticipantDisconnected(RemoteParticipantInterface* participant) override {
		std::lock_guard<std::mutex> guard(lock_);
		disconnected_identity_ = participant->Identity();
	}
	void OnParticipantMetadataChanged(const std::string& previous,
	                                  ParticipantInterface* participant) override {
		std::lock_guard<std::mutex> guard(lock_);
		previous_metadata_ = previous;
		metadata_identity_ = participant->Identity();
	}
	void OnParticipantNameChanged(const std::string& name,
	                              ParticipantInterface* participant) override {
		std::lock_guard<std::mutex> guard(lock_);
		name_ = name;
		name_identity_ = participant->Identity();
	}
	void OnParticipantAttributesChanged(const std::map<std::string, std::string>& changes,
	                                    ParticipantInterface* participant) override {
		std::lock_guard<std::mutex> guard(lock_);
		attribute_changes_ = changes;
		attributes_identity_ = participant->Identity();
	}

	bool connected(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return connected_identity_ == identity;
	}
	bool disconnected(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return disconnected_identity_ == identity;
	}
	bool metadata_changed(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return metadata_identity_ == identity;
	}
	bool name_changed(const std::string& identity, const std::string& name) {
		std::lock_guard<std::mutex> guard(lock_);
		return name_identity_ == identity && name_ == name;
	}
	bool attributes_changed(const std::string& identity, const std::string& key,
	                        const std::string& value) {
		std::lock_guard<std::mutex> guard(lock_);
		auto found = attribute_changes_.find(key);
		return attributes_identity_ == identity && found != attribute_changes_.end() &&
		       found->second == value;
	}

private:
	std::mutex lock_;
	std::string connected_identity_;
	std::string disconnected_identity_;
	std::string previous_metadata_;
	std::string metadata_identity_;
	std::string name_;
	std::string name_identity_;
	std::map<std::string, std::string> attribute_changes_;
	std::string attributes_identity_;
};

class ReconnectEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnReconnecting() override { reconnecting_.store(true); }
	void OnReconnected() override { reconnected_.store(true); }
	void OnDisconnected() override { disconnected_.store(true); }

	bool reconnecting() const { return reconnecting_.load(); }
	bool reconnected() const { return reconnected_.load(); }
	bool disconnected() const { return disconnected_.load(); }

private:
	std::atomic<bool> reconnecting_{false};
	std::atomic<bool> reconnected_{false};
	std::atomic<bool> disconnected_{false};
};

class TemporaryFile {
public:
	explicit TemporaryFile(const std::vector<uint8_t>& data) {
		path_ =
		    std::filesystem::temp_directory_path() /
		    ("livekit-cpp-integration-" +
		     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
		std::ofstream output(path_, std::ios::binary);
		output.write(reinterpret_cast<const char*>(data.data()),
		             static_cast<std::streamsize>(data.size()));
	}

	~TemporaryFile() {
		std::error_code error;
		std::filesystem::remove(path_, error);
	}

	const std::filesystem::path& path() const { return path_; }

private:
	std::filesystem::path path_;
};

TEST(LiveKitServerTest, ConnectsWithEnvironmentCredentials) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN_SINGLE");
	if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL and LIVEKIT_TOKEN_SINGLE to run the single-client "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	ASSERT_TRUE(room->Connect(url, token));
	ASSERT_NE(room->GetLocalParticipant(), nullptr);
	EXPECT_FALSE(room->Sid().empty());
	EXPECT_FALSE(room->Name().empty());
	EXPECT_FALSE(room->GetLocalParticipant()->Sid().empty());
	EXPECT_FALSE(room->GetLocalParticipant()->Identity().empty());
	EXPECT_TRUE(room->Disconnect());
	EXPECT_FALSE(room->IsConnected());
}

TEST(LiveKitServerTest, RecoversAfterSignalTransportDisconnect) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN_SINGLE");
	if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL and LIVEKIT_TOKEN_SINGLE to run the reconnect "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	ReconnectEvents events;
	room->AddEventListener(&events);
	ASSERT_TRUE(room->Connect(url, token));
	ASSERT_TRUE(WaitUntil([&] { return room->IsConnected(); }, std::chrono::seconds(10)));
	auto* concrete_room = dynamic_cast<Room*>(room.get());
	ASSERT_NE(concrete_room, nullptr);
	ASSERT_TRUE(concrete_room->SimulateSignalDisconnectForTesting());
	ASSERT_TRUE(WaitUntil([&] { return events.reconnecting(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected() && room->IsConnected(); },
	                      std::chrono::seconds(30)));
	EXPECT_FALSE(events.disconnected());
	EXPECT_FALSE(room->Sid().empty());
	EXPECT_FALSE(room->GetLocalParticipant()->Sid().empty());

	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
}

TEST(LiveKitServerTest, RepublishesAudioAfterReconnect) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the media "
		                "reconnect integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));
	const auto sender_identity = sender->GetLocalParticipant()->Identity();

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "reconnect-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions options;
	options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(audio_track.get(), options));
	std::vector<int16_t> samples(480, 1500);
	const auto initial_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < 3 && std::chrono::steady_clock::now() < initial_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	ASSERT_GE(events.audio_frame_count(), 3u);
	const auto frames_before_reconnect = events.audio_frame_count();

	auto* concrete_sender = dynamic_cast<Room*>(sender.get());
	ASSERT_NE(concrete_sender, nullptr);
	ASSERT_TRUE(concrete_sender->SimulateFullReconnectForTesting());
	ASSERT_TRUE(WaitUntil([&] { return events.reconnecting(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected() && sender->IsConnected(); },
	                      std::chrono::seconds(30)));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    auto* participant = receiver->GetRemoteParticipantByIdentity(sender_identity);
		    return participant != nullptr &&
		           participant->GetTrackPublication(TrackSource::Microphone) != nullptr;
	    },
	    std::chrono::seconds(10)));
	EXPECT_GE(events.local_tracks_published(), 2u);

	const auto recovered_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < frames_before_reconnect + 3 &&
	       std::chrono::steady_clock::now() < recovered_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_GE(events.audio_frame_count(), frames_before_reconnect + 3);
	const std::vector<uint8_t> publisher_recovered_data{1, 3, 5, 7};
	DataPublishOptions data_options;
	data_options.reliable = true;
	data_options.topic = "publisher-recovered";
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(publisher_recovered_data, data_options));
	ASSERT_TRUE(WaitUntil([&] {
		return events.received_data("publisher-recovered", publisher_recovered_data, true);
	}));

	const auto reconnecting_before_receiver = events.reconnecting_count();
	const auto reconnected_before_receiver = events.reconnected_count();
	const auto frames_before_receiver_reconnect = events.audio_frame_count();
	auto* concrete_receiver = dynamic_cast<Room*>(receiver.get());
	ASSERT_NE(concrete_receiver, nullptr);
	ASSERT_TRUE(concrete_receiver->SimulateSignalDisconnectForTesting());
	ASSERT_TRUE(
	    WaitUntil([&] { return events.reconnecting_count() > reconnecting_before_receiver; },
	              std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return events.reconnected_count() > reconnected_before_receiver &&
		           receiver->IsConnected();
	    },
	    std::chrono::seconds(30)));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    auto* participant = receiver->GetRemoteParticipantByIdentity(sender_identity);
		    return participant != nullptr &&
		           participant->GetTrackPublication(TrackSource::Microphone) != nullptr;
	    },
	    std::chrono::seconds(10)));
	const auto receiver_recovered_deadline =
	    std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < frames_before_receiver_reconnect + 3 &&
	       std::chrono::steady_clock::now() < receiver_recovered_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_GE(events.audio_frame_count(), frames_before_receiver_reconnect + 3);

	const std::vector<uint8_t> receiver_recovered_data{2, 4, 6, 8};
	data_options.topic = "receiver-recovered";
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(receiver_recovered_data, data_options));
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.received_data("receiver-recovered", receiver_recovered_data, true); }));

	const auto reconnecting_before_sender_resume = events.reconnecting_count();
	const auto reconnected_before_sender_resume = events.reconnected_count();
	const auto published_before_sender_resume = events.local_tracks_published();
	const auto frames_before_sender_resume = events.audio_frame_count();
	ASSERT_TRUE(concrete_sender->SimulateSignalDisconnectForTesting());
	ASSERT_TRUE(
	    WaitUntil([&] { return events.reconnecting_count() > reconnecting_before_sender_resume; },
	              std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return events.reconnected_count() > reconnected_before_sender_resume &&
		           sender->IsConnected();
	    },
	    std::chrono::seconds(30)));
	EXPECT_EQ(events.local_tracks_published(), published_before_sender_resume);
	const auto sender_resumed_deadline =
	    std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < frames_before_sender_resume + 3 &&
	       std::chrono::steady_clock::now() < sender_resumed_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_GE(events.audio_frame_count(), frames_before_sender_resume + 3);
	const std::vector<uint8_t> sender_resumed_data{9, 7, 5, 3};
	data_options.topic = "publisher-resumed";
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(sender_resumed_data, data_options));
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.received_data("publisher-resumed", sender_resumed_data, true); }));

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(audio_track.get()));
	audio_track.reset();
	audio_source.reset();
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, SynchronizesParticipantJoinAndLeave) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* first_token = std::getenv("LIVEKIT_TOKEN");
	const char* metadata_token = std::getenv("LIVEKIT_TOKEN_2_UPDATE");
	const char* second_token = metadata_token != nullptr && *metadata_token != '\0'
	                               ? metadata_token
	                               : std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || first_token == nullptr || second_token == nullptr || *url == '\0' ||
	    *first_token == '\0' || *second_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the "
		                "participant integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto first_room = CreateRoomUnique();
	auto second_room = CreateRoomUnique();
	ParticipantEvents events;
	first_room->AddEventListener(&events);
	ASSERT_TRUE(first_room->Connect(url, first_token));
	ASSERT_TRUE(WaitUntil([&] { return first_room->IsConnected(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(second_room->Connect(url, second_token));

	auto* first_local = first_room->GetLocalParticipant();
	auto* second_local = second_room->GetLocalParticipant();
	ASSERT_NE(first_local, nullptr);
	ASSERT_NE(second_local, nullptr);
	ASSERT_FALSE(first_local->Sid().empty());
	ASSERT_FALSE(second_local->Sid().empty());
	ASSERT_NE(first_local->Identity(), second_local->Identity());
	// A preceding integration process may still be inside LiveKit's reconnect grace period for the
	// same token identity. In that case the server reconciles the existing participant instead of
	// emitting a second joined transition, but the room snapshot must still converge.
	ASSERT_TRUE(WaitUntil([&] {
		return events.connected(second_local->Identity()) ||
		       first_room->GetRemoteParticipantByIdentity(second_local->Identity()) != nullptr;
	}));

	ASSERT_TRUE(WaitUntil(
	    [&] { return first_room->GetRemoteParticipantBySid(second_local->Sid()) != nullptr; }));
	EXPECT_EQ(first_room->GetRemoteParticipantBySid(second_local->Sid())->Identity(),
	          second_local->Identity());

	if (metadata_token != nullptr && *metadata_token != '\0') {
		ASSERT_TRUE(second_local->SetMetadata("cpp-integration-metadata"));
		ASSERT_TRUE(WaitUntil([&] {
			auto* participant = first_room->GetRemoteParticipantBySid(second_local->Sid());
			return participant != nullptr &&
			       participant->Metadata() == "cpp-integration-metadata" &&
			       events.metadata_changed(second_local->Identity());
		}));
		ASSERT_TRUE(second_local->SetName("cpp-integration-name"));
		ASSERT_TRUE(WaitUntil([&] {
			auto* participant = first_room->GetRemoteParticipantBySid(second_local->Sid());
			return participant != nullptr && participant->Name() == "cpp-integration-name" &&
			       events.name_changed(second_local->Identity(), "cpp-integration-name");
		}));
		ASSERT_TRUE(second_local->SetAttributes({{"client", "cpp"}}));
		ASSERT_TRUE(WaitUntil([&] {
			auto* participant = first_room->GetRemoteParticipantBySid(second_local->Sid());
			return participant != nullptr && participant->Attributes()["client"] == "cpp" &&
			       events.attributes_changed(second_local->Identity(), "client", "cpp");
		}));
	}

	ASSERT_TRUE(second_room->Disconnect());
	EXPECT_TRUE(WaitUntil(
	    [&] { return first_room->GetRemoteParticipantBySid(second_local->Sid()) == nullptr; }));
	EXPECT_TRUE(WaitUntil([&] { return events.disconnected(second_local->Identity()); }));
	first_room->RemoveEventListener();
	EXPECT_TRUE(first_room->Disconnect());
}

TEST(LiveKitServerTest, PublishesAndReceivesAudioAndVideo) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the media "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));
	EXPECT_EQ(receiver->State(), RoomInterface::RoomState::Connected);
	EXPECT_EQ(sender->State(), RoomInterface::RoomState::Connected);

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "integration-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions audio_options;
	audio_options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(audio_track.get(), audio_options));
	ASSERT_TRUE(WaitUntil([&] {
		auto* participant =
		    receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
		return participant != nullptr && participant->IsMicrophoneEnabled();
	}));
	ASSERT_TRUE(WaitUntil([&] { return events.local_track_subscribed(audio_track->Sid()); }))
	    << "publisher did not receive TrackSubscribed for " << audio_track->Sid();
	auto* sender_participant =
	    receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
	ASSERT_NE(sender_participant, nullptr);
	auto* audio_publication = sender_participant->GetTrackPublication(TrackSource::Microphone);
	ASSERT_NE(audio_publication, nullptr);
	EXPECT_EQ(receiver->GetRemoteParticipantByIdentity(sender->GetLocalParticipant()->Identity()),
	          sender_participant);
	EXPECT_EQ(audio_publication->Name(), "integration-audio");
	EXPECT_EQ(audio_publication->Kind(), TrackKind::Audio);
	EXPECT_FALSE(audio_publication->IsMuted());
	ASSERT_TRUE(sender->SetLocalTrackMuted(audio_track->Sid(), true));
	ASSERT_TRUE(WaitUntil([&] { return audio_publication->IsMuted(); }));
	ASSERT_TRUE(sender->SetLocalTrackMuted(audio_track->Sid(), false));
	ASSERT_TRUE(WaitUntil([&] { return !audio_publication->IsMuted(); }));

	std::vector<int16_t> audio_samples(480, 1500);
	const auto audio_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!events.audio_received() && std::chrono::steady_clock::now() < audio_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	ASSERT_TRUE(events.audio_received());

	VideoFrame video_frame;
	video_frame.width = 320;
	video_frame.height = 180;
	video_frame.data.resize(video_frame.width * video_frame.height * 3 / 2, 128);
	std::fill(video_frame.data.begin(),
	          video_frame.data.begin() + video_frame.width * video_frame.height, 64);
	video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                               std::chrono::steady_clock::now().time_since_epoch())
	                               .count();
	auto video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(video_source->CaptureFrame(video_frame));
	auto video_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "integration-video", video_source.get());
	ASSERT_NE(video_track, nullptr);
	TrackPublishOptions video_options;
	video_options.source = TrackSource::Camera;
	video_options.simulcast = false;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(video_track.get(), video_options));

	const auto video_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!events.video_received() && std::chrono::steady_clock::now() < video_deadline) {
		video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                               std::chrono::steady_clock::now().time_since_epoch())
		                               .count();
		ASSERT_TRUE(video_source->CaptureFrame(video_frame));
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	ASSERT_TRUE(events.video_received())
	    << "subscribed=" << events.video_subscribed() << ", frames=" << events.video_frame_count()
	    << ", last_size=" << events.last_video_width() << 'x' << events.last_video_height();
	ASSERT_TRUE(WaitUntil(
	    [&] { return sender_participant->GetTrackPublication(TrackSource::Camera) != nullptr; },
	    std::chrono::seconds(10)))
	    << PublicationSummary(sender_participant);
	auto* video_publication = sender_participant->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(video_publication, nullptr);
	EXPECT_EQ(video_publication->SubscriptionStatus(), TrackSubscriptionStatus::Subscribed);
	RemoteTrackSettings remote_settings;
	remote_settings.video_dimensions = TrackDimensions{160, 90};
	remote_settings.video_fps = 15;
	remote_settings.priority = 1;
	ASSERT_TRUE(receiver->UpdateRemoteTrackSettings(sender_participant->Sid(),
	                                                video_publication->Sid(), remote_settings));
	const auto retained_settings = video_publication->GetRemoteTrackSettings();
	ASSERT_TRUE(retained_settings.video_dimensions.has_value());
	EXPECT_EQ(retained_settings.video_dimensions->width, 160u);
	EXPECT_EQ(retained_settings.video_dimensions->height, 90u);
	EXPECT_EQ(retained_settings.video_fps, 15u);
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(sender_participant->Sid(),
	                                               video_publication->Sid(), false));
	EXPECT_EQ(video_publication->SubscriptionStatus(), TrackSubscriptionStatus::Unsubscribed);
	EXPECT_TRUE(events.subscription_status(video_publication->Sid(),
	                                       TrackSubscriptionStatus::Unsubscribed));
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(sender_participant->Sid(),
	                                               video_publication->Sid(), true));
	EXPECT_EQ(video_publication->SubscriptionStatus(), TrackSubscriptionStatus::Subscribed);
	EXPECT_TRUE(
	    events.subscription_status(video_publication->Sid(), TrackSubscriptionStatus::Subscribed));
	EXPECT_EQ(video_publication->Name(), "integration-video");
	EXPECT_EQ(video_publication->Kind(), TrackKind::Video);
	EXPECT_TRUE(sender_participant->IsCameraEnabled());

	std::vector<LocalTrackInterface*> video_tracks{video_track.get()};
	ASSERT_EQ(sender->GetLocalParticipant()->UnpublishTracks(video_tracks, false), 1u);
	EXPECT_TRUE(video_track->IsEnabled());
	ASSERT_TRUE(WaitUntil(
	    [&] { return sender_participant->GetTrackPublication(TrackSource::Camera) == nullptr; }));
	EXPECT_GT(events.video_unsubscribed_count(), 0u);
	ASSERT_TRUE(sender->GetLocalParticipant()->RepublishAllTracks());
	ASSERT_TRUE(WaitUntil([&] {
		return sender_participant->GetTrackPublication(TrackSource::Microphone) != nullptr;
	}));
	EXPECT_GE(events.local_tracks_published(), 3u);
	EXPECT_GE(events.local_tracks_unpublished(), 2u);
	ASSERT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(audio_track.get()));
	EXPECT_FALSE(audio_track->IsEnabled());
	ASSERT_TRUE(WaitUntil([&] {
		return sender_participant->GetTrackPublication(TrackSource::Microphone) == nullptr;
	}));

	video_track.reset();
	video_source.reset();
	audio_track.reset();
	audio_source.reset();
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, TransfersDataAndFileWithoutMediaTracks) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the data "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));

	const std::vector<uint8_t> data_payload{'l', 'i', 'v', 'e', 'k', 'i', 't'};
	DataPublishOptions data_options;
	data_options.topic = "integration-data";
	data_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(data_payload, data_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_data("integration-data", data_payload, true); }));
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishDtmf(11, "#"));
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.received_dtmf(11, "#", sender->GetLocalParticipant()->Identity()); }));
	auto chat = sender->GetLocalParticipant()->SendChatMessage("hello from structured chat");
	ASSERT_TRUE(chat.has_value());
	ASSERT_FALSE(chat->id.empty());
	ASSERT_TRUE(WaitUntil([&] {
		return events.received_chat(chat->id, chat->message, false,
		                            sender->GetLocalParticipant()->Identity());
	}));
	auto edited_chat =
	    sender->GetLocalParticipant()->EditChatMessage("edited structured chat", *chat);
	ASSERT_TRUE(edited_chat.has_value());
	ASSERT_TRUE(edited_chat->edit_timestamp.has_value());
	ASSERT_TRUE(WaitUntil([&] {
		return events.received_chat(chat->id, edited_chat->message, true,
		                            sender->GetLocalParticipant()->Identity());
	}));

	const std::vector<uint8_t> lossy_payload{'l', 'o', 's', 's', 'y'};
	data_options.reliable = false;
	data_options.topic = "integration-lossy";
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(lossy_payload, data_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_data("integration-lossy", lossy_payload, false); }));

	TextSendOptions text_options;
	text_options.topic = "integration-text";
	text_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->SendText("hello from C++", text_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_text("integration-text", "hello from C++"); }));

	const std::vector<uint8_t> byte_payload{'b', 'y', 't', 'e', 's'};
	ByteSendOptions byte_options;
	byte_options.topic = "integration-bytes";
	byte_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->SendBytes(byte_payload, byte_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_bytes("integration-bytes", byte_payload); }));

	std::mutex stream_mutex;
	std::vector<TextStreamEvent> text_stream_events;
	std::vector<ByteStreamEvent> byte_stream_events;
	ASSERT_TRUE(receiver->RegisterTextStreamHandler(
	    "integration-stream-text", [&](const TextStreamEvent& event) {
		    std::lock_guard<std::mutex> guard(stream_mutex);
		    text_stream_events.push_back(event);
	    }));
	ASSERT_TRUE(receiver->RegisterByteStreamHandler(
	    "integration-stream-bytes", [&](const ByteStreamEvent& event) {
		    std::lock_guard<std::mutex> guard(stream_mutex);
		    byte_stream_events.push_back(event);
	    }));
	const std::string streamed_text = "incremental text over two writes";
	uint64_t text_progress = 0;
	StreamTextOptions stream_text_options;
	stream_text_options.topic = "integration-stream-text";
	stream_text_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	stream_text_options.total_size = streamed_text.size();
	stream_text_options.on_progress = [&](uint64_t sent, std::optional<uint64_t>) {
		text_progress = sent;
	};
	auto text_writer = sender->GetLocalParticipant()->StreamText(stream_text_options);
	ASSERT_NE(text_writer, nullptr);
	ASSERT_TRUE(text_writer->Write("incremental text "));
	ASSERT_TRUE(text_writer->Write("over two writes"));
	ASSERT_TRUE(text_writer->Close());
	EXPECT_EQ(text_progress, streamed_text.size());
	ASSERT_TRUE(WaitUntil([&] {
		std::lock_guard<std::mutex> guard(stream_mutex);
		return !text_stream_events.empty() &&
		       text_stream_events.back().type == DataStreamEventType::Closed;
	}));
	{
		std::lock_guard<std::mutex> guard(stream_mutex);
		std::string received;
		for (const auto& event : text_stream_events) {
			if (event.type == DataStreamEventType::Chunk) {
				received += event.content;
			}
		}
		EXPECT_EQ(received, streamed_text);
		EXPECT_EQ(text_stream_events.front().info.participant_identity,
		          sender->GetLocalParticipant()->Identity());
	}

	StreamBytesOptions stream_byte_options;
	stream_byte_options.topic = "integration-stream-bytes";
	stream_byte_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	auto byte_writer = sender->GetLocalParticipant()->StreamBytes(stream_byte_options);
	ASSERT_NE(byte_writer, nullptr);
	ASSERT_TRUE(byte_writer->Write(std::vector<uint8_t>{1, 2, 3, 4}));
	ASSERT_TRUE(byte_writer->Cancel("integration cancellation"));
	ASSERT_TRUE(WaitUntil([&] {
		std::lock_guard<std::mutex> guard(stream_mutex);
		return !byte_stream_events.empty() &&
		       byte_stream_events.back().type == DataStreamEventType::Failed;
	}));
	{
		std::lock_guard<std::mutex> guard(stream_mutex);
		EXPECT_EQ(byte_stream_events.back().reason, "integration cancellation");
	}
	EXPECT_TRUE(receiver->UnregisterTextStreamHandler("integration-stream-text"));
	EXPECT_TRUE(receiver->UnregisterByteStreamHandler("integration-stream-bytes"));

	std::atomic<uint64_t> large_stream_bytes{0};
	std::atomic<bool> large_stream_closed{false};
	ASSERT_TRUE(receiver->RegisterByteStreamHandler(
	    "integration-large-stream", [&](const ByteStreamEvent& event) {
		    if (event.type == DataStreamEventType::Chunk) {
			    large_stream_bytes.fetch_add(event.content.size());
		    } else if (event.type == DataStreamEventType::Closed) {
			    large_stream_closed = true;
		    }
	    }));
	constexpr std::size_t large_stream_size = 6 * 1024 * 1024;
	StreamBytesOptions large_options;
	large_options.topic = "integration-large-stream";
	large_options.total_size = large_stream_size;
	large_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	auto large_writer = sender->GetLocalParticipant()->StreamBytes(large_options);
	ASSERT_NE(large_writer, nullptr);
	const std::vector<uint8_t> large_chunk(64 * 1024, 0x5a);
	for (std::size_t sent = 0; sent < large_stream_size; sent += large_chunk.size()) {
		ASSERT_TRUE(large_writer->Write(large_chunk));
	}
	ASSERT_TRUE(large_writer->Close());
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return large_stream_closed.load() && large_stream_bytes.load() == large_stream_size;
	    },
	    std::chrono::seconds(20)));
	EXPECT_TRUE(receiver->UnregisterByteStreamHandler("integration-large-stream"));

	std::vector<uint8_t> file_payload(40 * 1024);
	for (std::size_t i = 0; i < file_payload.size(); ++i) {
		file_payload[i] = static_cast<uint8_t>(i % 251);
	}
	TemporaryFile file(file_payload);
	FileSendOptions file_options;
	file_options.topic = "integration-file";
	file_options.mime_type = "application/x-livekit-test";
	file_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->SendFile(file.path().string(), file_options));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return events.received_file(file.path().filename().string(),
		                                "application/x-livekit-test", "integration-file",
		                                file_payload);
	    },
	    std::chrono::seconds(10)));

	receiver->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, EnforcesTrackSubscriptionPermissions) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the "
		                "subscription permission integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));

	auto source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique("permission-audio",
	                                                                        source.get());
	ASSERT_NE(track, nullptr);
	TrackPublishOptions options;
	options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(track.get(), options));
	auto* publication = sender->GetLocalParticipant()->GetTrackPublication(TrackSource::Microphone);
	ASSERT_NE(publication, nullptr);
	const auto track_sid = publication->Sid();
	ASSERT_FALSE(track_sid.empty());

	std::vector<int16_t> samples(480, 1200);
	const auto initial_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < 3 && std::chrono::steady_clock::now() < initial_deadline) {
		ASSERT_TRUE(source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	ASSERT_GE(events.audio_frame_count(), 3u);

	ParticipantTrackPermission receiver_permission;
	receiver_permission.participant_identity = receiver->GetLocalParticipant()->Identity();
	ASSERT_TRUE(sender->GetLocalParticipant()->SetTrackSubscriptionPermissions(
	    false, {receiver_permission}));
	ASSERT_TRUE(WaitUntil([&] { return events.permission_changed(track_sid, false); }));
	auto* remote_sender =
	    receiver->GetRemoteParticipantByIdentity(sender->GetLocalParticipant()->Identity());
	ASSERT_NE(remote_sender, nullptr);
	auto* remote_publication = remote_sender->GetTrackPublication(TrackSource::Microphone);
	ASSERT_NE(remote_publication, nullptr);
	EXPECT_FALSE(remote_publication->IsSubscriptionAllowed());
	ASSERT_TRUE(WaitUntil([&] { return events.audio_unsubscribed_count() > 0; }));

	receiver_permission.allow_all = true;
	const auto subscriptions_before_grant = events.audio_subscribed_count();
	ASSERT_TRUE(sender->GetLocalParticipant()->SetTrackSubscriptionPermissions(
	    false, {receiver_permission}));
	ASSERT_TRUE(WaitUntil([&] { return events.permission_changed(track_sid, true, 2); }));
	EXPECT_TRUE(remote_publication->IsSubscriptionAllowed());
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(remote_sender->Sid(), track_sid, true));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.audio_subscribed_count() > subscriptions_before_grant; }));
	const auto frames_before_grant = events.audio_frame_count();
	const auto grant_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (events.audio_frame_count() < frames_before_grant + 3 &&
	       std::chrono::steady_clock::now() < grant_deadline) {
		ASSERT_TRUE(source->CaptureFrame(samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_GE(events.audio_frame_count(), frames_before_grant + 3);

	ASSERT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(track.get()));
	track.reset();
	source.reset();
	receiver->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, PerformsRpcBetweenParticipants) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the RPC "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	ASSERT_TRUE(
	    receiver->RegisterRpcMethod("integration.echo", [](const RpcInvocationData& invocation) {
		    return RpcResult::Success("echo:" + invocation.payload + ":" +
		                              invocation.caller_identity);
	    }));
	ASSERT_TRUE(receiver->RegisterRpcMethod("integration.error", [](const RpcInvocationData&) {
		return RpcResult::Failure(
		    {RpcErrorCode::ApplicationError, "expected application error", "details"});
	}));
	ASSERT_TRUE(receiver->RegisterRpcMethod("integration.large", [](const RpcInvocationData&) {
		return RpcResult::Success(std::string(kMaximumRpcPayloadBytes + 1, 'x'));
	}));

	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));
	const auto receiver_identity = receiver->GetLocalParticipant()->Identity();
	const auto sender_identity = sender->GetLocalParticipant()->Identity();
	ASSERT_FALSE(receiver_identity.empty());

	PerformRpcParams params;
	params.destination_identity = receiver_identity;
	params.method = "integration.echo";
	params.payload = "hello";
	params.response_timeout = std::chrono::seconds(10);
	auto result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_TRUE(result.Ok()) << (result.error ? result.error->message : "unknown error");
	EXPECT_EQ(result.payload, "echo:hello:" + sender_identity);

	params.method = "integration.missing";
	result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, RpcErrorCode::UnsupportedMethod);

	params.destination_identity = "missing-rpc-participant";
	params.method = "integration.echo";
	result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, RpcErrorCode::RecipientNotFound);
	params.destination_identity = receiver_identity;

	params.method = "integration.error";
	result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, RpcErrorCode::ApplicationError);
	EXPECT_EQ(result.error->message, "expected application error");
	EXPECT_EQ(result.error->data, "details");

	params.method = "integration.large";
	result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, RpcErrorCode::ResponsePayloadTooLarge);

	params.method = "integration.echo";
	params.payload.assign(kMaximumRpcPayloadBytes + 1, 'x');
	result = sender->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	EXPECT_EQ(result.error->code, RpcErrorCode::RequestPayloadTooLarge);

	EXPECT_TRUE(receiver->UnregisterRpcMethod("integration.echo"));
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

} // namespace
} // namespace livekit::core
