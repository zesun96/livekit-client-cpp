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

	void OnTrackSubscribed(RemoteTrackInterface* track, RemoteParticipantInterface*) override {
		if (track->Kind() == TrackKind::Audio) {
			audio_subscribed_.store(true);
		} else if (track->Kind() == TrackKind::Video) {
			video_subscribed_.store(true);
		}
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

	bool audio_received() const { return audio_subscribed_.load() && audio_frames_.load() >= 5; }
	bool video_received() const { return video_subscribed_.load() && video_frames_.load() >= 3; }
	bool video_subscribed() const { return video_subscribed_.load(); }
	uint64_t video_frame_count() const { return video_frames_.load(); }
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

private:
	std::atomic<bool> audio_subscribed_{false};
	std::atomic<bool> video_subscribed_{false};
	std::atomic<uint64_t> audio_frames_{0};
	std::atomic<uint64_t> video_frames_{0};
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
	ASSERT_TRUE(WaitUntil([&] { return events.connected(second_local->Identity()); }));

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
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(sender_participant->Sid(),
	                                               video_publication->Sid(), false));
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(sender_participant->Sid(),
	                                               video_publication->Sid(), true));
	EXPECT_EQ(video_publication->Name(), "integration-video");
	EXPECT_EQ(video_publication->Kind(), TrackKind::Video);
	EXPECT_TRUE(sender_participant->IsCameraEnabled());

	std::vector<LocalTrackInterface*> video_tracks{video_track.get()};
	ASSERT_EQ(sender->GetLocalParticipant()->UnpublishTracks(video_tracks, false), 1u);
	EXPECT_TRUE(video_track->IsEnabled());
	ASSERT_TRUE(WaitUntil(
	    [&] { return sender_participant->GetTrackPublication(TrackSource::Camera) == nullptr; }));
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

} // namespace
} // namespace livekit::core
