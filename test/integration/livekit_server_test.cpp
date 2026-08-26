#include "../../src/core/participant/local_participant.h"
#include "../../src/core/room.h"
#include "../../src/core/track/local_video_track.h"
#include "../../src/core/track/track_publication.h"
#include "livekit/capi/livekit.h"
#include "livekit/core/livekit_client.h"
#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/participant/remote_participant_interface.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/remote_track_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
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

class CApiLogCapture {
public:
	void Add(const lk_log_record_t* record) {
		if (record == nullptr || record->message == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(mutex_);
		records_.emplace_back(record->source, record->message);
	}

	bool Contains(lk_log_source_t source, std::string_view text) const {
		std::lock_guard<std::mutex> guard(mutex_);
		return std::any_of(records_.begin(), records_.end(), [&](const auto& record) {
			return record.first == source && record.second.find(text) != std::string::npos;
		});
	}

	bool ContainsSource(lk_log_source_t source) const {
		std::lock_guard<std::mutex> guard(mutex_);
		return std::any_of(records_.begin(), records_.end(),
		                   [&](const auto& record) { return record.first == source; });
	}

	bool ContainsValue(std::string_view value) const {
		std::lock_guard<std::mutex> guard(mutex_);
		return std::any_of(records_.begin(), records_.end(), [&](const auto& record) {
			return !value.empty() && record.second.find(value) != std::string::npos;
		});
	}

private:
	mutable std::mutex mutex_;
	std::vector<std::pair<lk_log_source_t, std::string>> records_;
};

void CaptureCApiLog(void* user_data, const lk_log_record_t* record) {
	if (user_data != nullptr) {
		static_cast<CApiLogCapture*>(user_data)->Add(record);
	}
}

class CApiLogCallbackGuard {
public:
	~CApiLogCallbackGuard() { lk_log_set_callback(nullptr, nullptr); }
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

struct CApiE2eeEvents {
	std::atomic_size_t audio_frames{0};
	std::atomic_bool data_received{false};
	std::atomic_bool sender_cryptor_ok{false};
	std::atomic_bool receiver_cryptor_ok{false};
};

class CApiDataTrackEvents {
public:
	void Published(const lk_data_track_info_t* track, const lk_participant_info_t* participant,
	               bool local) {
		if (track == nullptr || participant == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		if (local) {
			local_published_ = true;
		} else {
			remote_published_ = true;
			remote_sid_ = track->sid;
			publisher_identity_ = participant->identity;
			remote_name_ = track->name;
			remote_has_json_ = track->has_frame_encoding &&
			                   track->frame_encoding == LK_DATA_TRACK_FRAME_ENCODING_JSON;
			remote_has_schema_ =
			    track->has_schema && std::string(track->schema_name) == "c.telemetry.v1" &&
			    track->schema_encoding == LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA;
		}
	}

	void Unpublished(bool local) {
		std::lock_guard<std::mutex> guard(lock_);
		if (local) {
			local_unpublished_ = true;
		} else {
			remote_unpublished_ = true;
		}
	}

	void Frame(const lk_data_track_frame_view_t* frame) {
		if (frame == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		++frame_count_;
		last_frame_.clear();
		if (frame->data != nullptr && frame->data_size != 0) {
			last_frame_.assign(frame->data, frame->data + frame->data_size);
		}
		last_timestamp_ = frame->has_user_timestamp ? frame->user_timestamp : 0;
	}

	bool remote_published(std::string& sid, std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		if (!remote_published_ || remote_name_ != "c-api-telemetry" || !remote_has_json_ ||
		    !remote_has_schema_) {
			return false;
		}
		sid = remote_sid_;
		identity = publisher_identity_;
		return true;
	}

	bool local_published() {
		std::lock_guard<std::mutex> guard(lock_);
		return local_published_;
	}

	bool frame_received(const std::vector<uint8_t>& payload, uint64_t timestamp,
	                    size_t minimum_count) {
		std::lock_guard<std::mutex> guard(lock_);
		return frame_count_ >= minimum_count && last_frame_ == payload &&
		       last_timestamp_ == timestamp;
	}

	bool unpublished() {
		std::lock_guard<std::mutex> guard(lock_);
		return local_unpublished_ && remote_unpublished_;
	}

private:
	std::mutex lock_;
	bool local_published_ = false;
	bool remote_published_ = false;
	bool local_unpublished_ = false;
	bool remote_unpublished_ = false;
	bool remote_has_json_ = false;
	bool remote_has_schema_ = false;
	std::string remote_sid_;
	std::string remote_name_;
	std::string publisher_identity_;
	std::vector<uint8_t> last_frame_;
	uint64_t last_timestamp_ = 0;
	size_t frame_count_ = 0;
};

void OnCApiRemoteDataTrackPublished(void* user_data, lk_room_t*, const lk_data_track_info_t* track,
                                    const lk_participant_info_t* participant) {
	static_cast<CApiDataTrackEvents*>(user_data)->Published(track, participant, false);
}

void OnCApiRemoteDataTrackUnpublished(void* user_data, lk_room_t*, const lk_data_track_info_t*,
                                      const lk_participant_info_t*) {
	static_cast<CApiDataTrackEvents*>(user_data)->Unpublished(false);
}

void OnCApiLocalDataTrackPublished(void* user_data, lk_room_t*, const lk_data_track_info_t* track,
                                   const lk_participant_info_t* participant) {
	static_cast<CApiDataTrackEvents*>(user_data)->Published(track, participant, true);
}

void OnCApiLocalDataTrackUnpublished(void* user_data, lk_room_t*, const lk_data_track_info_t*,
                                     const lk_participant_info_t*) {
	static_cast<CApiDataTrackEvents*>(user_data)->Unpublished(true);
}

void OnCApiDataTrackFrame(void* user_data, lk_room_t*, const lk_data_track_info_t*,
                          const lk_participant_info_t*, const lk_data_track_frame_view_t* frame) {
	static_cast<CApiDataTrackEvents*>(user_data)->Frame(frame);
}

class CApiParticipantEvents {
public:
	void Connected(const lk_participant_info_t* participant) {
		if (participant == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		connected_identity_ = participant->identity;
	}

	void MetadataChanged(const char* previous_metadata, const lk_participant_info_t* participant) {
		if (previous_metadata == nullptr || participant == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		metadata_identity_ = participant->identity;
		previous_metadata_ = previous_metadata;
		metadata_ = participant->metadata;
	}

	void NameChanged(const char* name, const lk_participant_info_t* participant) {
		if (name == nullptr || participant == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		name_identity_ = participant->identity;
		name_ = name;
		participant_name_ = participant->name;
	}

	void AttributesChanged(const lk_attribute_t* changes, size_t change_count,
	                       const lk_participant_info_t* participant) {
		if ((changes == nullptr && change_count != 0) || participant == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		attributes_identity_ = participant->identity;
		attribute_changes_.clear();
		for (size_t index = 0; index < change_count; ++index) {
			attribute_changes_[changes[index].key] = changes[index].value;
		}
		++attribute_event_count_;
	}

	bool connected(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return connected_identity_ == identity;
	}

	bool metadata_changed(const std::string& identity, const std::string& previous,
	                      const std::string& current) {
		std::lock_guard<std::mutex> guard(lock_);
		return metadata_identity_ == identity && previous_metadata_ == previous &&
		       metadata_ == current;
	}

	bool name_changed(const std::string& identity, const std::string& name) {
		std::lock_guard<std::mutex> guard(lock_);
		return name_identity_ == identity && name_ == name && participant_name_ == name;
	}

	bool attributes_changed(const std::string& identity,
	                        const std::map<std::string, std::string>& expected,
	                        uint64_t minimum_event_count) {
		std::lock_guard<std::mutex> guard(lock_);
		return attributes_identity_ == identity && attribute_changes_ == expected &&
		       attribute_event_count_ >= minimum_event_count;
	}

private:
	std::mutex lock_;
	std::string connected_identity_;
	std::string metadata_identity_;
	std::string previous_metadata_;
	std::string metadata_;
	std::string name_identity_;
	std::string name_;
	std::string participant_name_;
	std::string attributes_identity_;
	std::map<std::string, std::string> attribute_changes_;
	uint64_t attribute_event_count_ = 0;
};

void OnCApiParticipantConnected(void* user_data, lk_room_t*,
                                const lk_participant_info_t* participant) {
	static_cast<CApiParticipantEvents*>(user_data)->Connected(participant);
}

void OnCApiParticipantMetadataChanged(void* user_data, lk_room_t*, const char* previous_metadata,
                                      const lk_participant_info_t* participant) {
	static_cast<CApiParticipantEvents*>(user_data)->MetadataChanged(previous_metadata, participant);
}

void OnCApiParticipantNameChanged(void* user_data, lk_room_t*, const char* name,
                                  const lk_participant_info_t* participant) {
	static_cast<CApiParticipantEvents*>(user_data)->NameChanged(name, participant);
}

void OnCApiParticipantAttributesChanged(void* user_data, lk_room_t*, const lk_attribute_t* changes,
                                        size_t change_count,
                                        const lk_participant_info_t* participant) {
	static_cast<CApiParticipantEvents*>(user_data)->AttributesChanged(changes, change_count,
	                                                                  participant);
}

class CApiDataStreamEvents {
public:
	void Text(const lk_text_received_t* event) {
		if (event == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		text_ = event->text;
		text_topic_ = event->topic;
		text_identity_ = event->participant_identity;
		text_reply_to_ = event->reply_to_stream_id;
		text_timestamp_ = event->timestamp;
		text_attached_stream_ids_ =
		    Strings(event->attached_stream_ids, event->attached_stream_id_count);
		text_attributes_ = Attributes(event->attributes, event->attribute_count);
	}

	void Bytes(const lk_file_received_t* event) {
		if (event == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		bytes_.assign(event->data, event->data + event->data_size);
		byte_name_ = event->name;
		byte_mime_type_ = event->mime_type;
		byte_topic_ = event->topic;
		byte_identity_ = event->participant_identity;
		byte_timestamp_ = event->timestamp;
		byte_attributes_ = Attributes(event->attributes, event->attribute_count);
	}

	void TextStream(const lk_text_stream_event_t* event) {
		if (event == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		if (event->type == LK_DATA_STREAM_EVENT_OPEN) {
			stream_text_topic_ = event->topic;
			stream_text_identity_ = event->participant_identity;
			stream_text_reply_to_ = event->reply_to_stream_id;
			stream_text_timestamp_ = event->timestamp;
			stream_text_attached_stream_ids_ =
			    Strings(event->attached_stream_ids, event->attached_stream_id_count);
			stream_text_attributes_ = Attributes(event->attributes, event->attribute_count);
		} else if (event->type == LK_DATA_STREAM_EVENT_CHUNK) {
			stream_text_.append(event->content, event->content_size);
		} else if (event->type == LK_DATA_STREAM_EVENT_CLOSED) {
			stream_text_closed_ = true;
		}
	}

	void ByteStream(const lk_byte_stream_event_t* event) {
		if (event == nullptr) {
			return;
		}
		std::lock_guard<std::mutex> guard(lock_);
		if (event->type == LK_DATA_STREAM_EVENT_OPEN) {
			stream_byte_name_ = event->name;
			stream_byte_mime_type_ = event->mime_type;
			stream_byte_topic_ = event->topic;
			stream_byte_identity_ = event->participant_identity;
			stream_byte_timestamp_ = event->timestamp;
			stream_byte_attributes_ = Attributes(event->attributes, event->attribute_count);
		} else if (event->type == LK_DATA_STREAM_EVENT_CHUNK) {
			stream_bytes_.insert(stream_bytes_.end(), event->content,
			                     event->content + event->content_size);
		} else if (event->type == LK_DATA_STREAM_EVENT_CLOSED) {
			stream_bytes_closed_ = true;
		}
	}

	bool LegacyTextMatches(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return text_ == "C API metadata" && text_topic_ == "c-api-text-metadata" &&
		       text_identity_ == identity && text_reply_to_ == "ST_reply" && text_timestamp_ > 0 &&
		       text_attached_stream_ids_ == std::vector<std::string>{"ST_attachment"} &&
		       text_attributes_ ==
		           std::map<std::string, std::string>{{"language", "zh-CN"}, {"purpose", "parity"}};
	}

	bool LegacyBytesMatch(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return bytes_ == std::vector<uint8_t>({4, 3, 2, 1}) && byte_name_ == "metadata.bin" &&
		       byte_mime_type_ == "application/x-livekit-metadata" &&
		       byte_topic_ == "c-api-byte-metadata" && byte_identity_ == identity &&
		       byte_timestamp_ > 0 &&
		       byte_attributes_ == std::map<std::string, std::string>{{"purpose", "parity"}};
	}

	bool TextStreamMatches(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return stream_text_closed_ && stream_text_ == "stream metadata" &&
		       stream_text_topic_ == "c-api-stream-text" && stream_text_identity_ == identity &&
		       stream_text_reply_to_ == "ST_stream_reply" && stream_text_timestamp_ > 0 &&
		       stream_text_attached_stream_ids_ ==
		           std::vector<std::string>{"ST_stream_attachment"} &&
		       stream_text_attributes_ ==
		           std::map<std::string, std::string>{{"language", "zh-CN"}, {"purpose", "stream"}};
	}

	bool ByteStreamMatches(const std::string& identity) {
		std::lock_guard<std::mutex> guard(lock_);
		return stream_bytes_closed_ && stream_bytes_ == std::vector<uint8_t>({1, 3, 5, 7}) &&
		       stream_byte_name_ == "stream.bin" &&
		       stream_byte_mime_type_ == "application/x-livekit-stream" &&
		       stream_byte_topic_ == "c-api-stream-bytes" && stream_byte_identity_ == identity &&
		       stream_byte_timestamp_ > 0 &&
		       stream_byte_attributes_ == std::map<std::string, std::string>{{"purpose", "stream"}};
	}

private:
	static std::vector<std::string> Strings(const char* const* values, size_t count) {
		std::vector<std::string> result;
		if (values == nullptr) {
			return result;
		}
		result.reserve(count);
		for (size_t index = 0; index < count; ++index) {
			result.emplace_back(values[index] != nullptr ? values[index] : "");
		}
		return result;
	}

	static std::map<std::string, std::string> Attributes(const lk_attribute_t* values,
	                                                     size_t count) {
		std::map<std::string, std::string> result;
		if (values == nullptr) {
			return result;
		}
		for (size_t index = 0; index < count; ++index) {
			result[values[index].key] = values[index].value;
		}
		return result;
	}

	std::mutex lock_;
	std::string text_;
	std::string text_topic_;
	std::string text_identity_;
	std::string text_reply_to_;
	int64_t text_timestamp_ = 0;
	std::vector<std::string> text_attached_stream_ids_;
	std::map<std::string, std::string> text_attributes_;
	std::vector<uint8_t> bytes_;
	std::string byte_name_;
	std::string byte_mime_type_;
	std::string byte_topic_;
	std::string byte_identity_;
	int64_t byte_timestamp_ = 0;
	std::map<std::string, std::string> byte_attributes_;
	bool stream_text_closed_ = false;
	std::string stream_text_;
	std::string stream_text_topic_;
	std::string stream_text_identity_;
	std::string stream_text_reply_to_;
	int64_t stream_text_timestamp_ = 0;
	std::vector<std::string> stream_text_attached_stream_ids_;
	std::map<std::string, std::string> stream_text_attributes_;
	bool stream_bytes_closed_ = false;
	std::vector<uint8_t> stream_bytes_;
	std::string stream_byte_name_;
	std::string stream_byte_mime_type_;
	std::string stream_byte_topic_;
	std::string stream_byte_identity_;
	int64_t stream_byte_timestamp_ = 0;
	std::map<std::string, std::string> stream_byte_attributes_;
};

void OnCApiTextReceived(void* user_data, lk_room_t*, const lk_text_received_t* event) {
	static_cast<CApiDataStreamEvents*>(user_data)->Text(event);
}

void OnCApiBytesReceived(void* user_data, lk_room_t*, const lk_file_received_t* event) {
	static_cast<CApiDataStreamEvents*>(user_data)->Bytes(event);
}

void OnCApiTextStream(void* user_data, lk_room_t*, const lk_text_stream_event_t* event) {
	static_cast<CApiDataStreamEvents*>(user_data)->TextStream(event);
}

void OnCApiByteStream(void* user_data, lk_room_t*, const lk_byte_stream_event_t* event) {
	static_cast<CApiDataStreamEvents*>(user_data)->ByteStream(event);
}

void OnCApiE2eeAudioFrame(void* user_data, lk_room_t*, const lk_track_publication_info_t*,
                          const lk_participant_info_t*, const lk_audio_frame_t*) {
	static_cast<CApiE2eeEvents*>(user_data)->audio_frames.fetch_add(1);
}

void OnCApiE2eeData(void* user_data, lk_room_t*, const lk_data_received_t* event) {
	constexpr std::array<uint8_t, 10> expected{'c', '-', 'e', '2', 'e', 'e', '-', 'd', 'a', 't'};
	if (event != nullptr && event->data_size == expected.size() &&
	    std::equal(expected.begin(), expected.end(), event->data)) {
		static_cast<CApiE2eeEvents*>(user_data)->data_received.store(true);
	}
}

void OnCApiEncryptionState(void* user_data, lk_room_t*, const lk_encryption_state_t* state) {
	if (state == nullptr || state->state != LK_FRAME_CRYPTOR_STATE_OK) {
		return;
	}
	auto* events = static_cast<CApiE2eeEvents*>(user_data);
	if (state->direction == LK_FRAME_CRYPTOR_DIRECTION_SENDER) {
		events->sender_cryptor_ok.store(true);
	} else {
		events->receiver_cryptor_ok.store(true);
	}
}

bool NotifyExternalHarness(const char* environment_name) {
	const char* path = std::getenv(environment_name);
	if (path == nullptr || *path == '\0') {
		return false;
	}
	std::ofstream marker(path, std::ios::binary | std::ios::trunc);
	marker << "ready\n";
	return marker.good();
}

VideoCodec VideoCodecFromEnvironment(std::string& mime_type) {
	const char* value = std::getenv("LIVEKIT_VIDEO_CODEC");
	std::string codec = value != nullptr ? value : "vp8";
	std::transform(codec.begin(), codec.end(), codec.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	if (codec == "h264") {
		mime_type = "video/H264";
		return VideoCodec::H264;
	}
	if (codec == "vp9") {
		mime_type = "video/VP9";
		return VideoCodec::VP9;
	}
	if (codec == "av1") {
		mime_type = "video/AV1";
		return VideoCodec::AV1;
	}
	mime_type = "video/VP8";
	return VideoCodec::VP8;
}

E2eeKey IntegrationE2eeKey() {
	return {
	    0x50, 0xf7, 0x32, 0x6a, 0x9d, 0x83, 0x11, 0xc4, 0x44, 0x29, 0x7e,
	    0x31, 0x6b, 0x05, 0xd8, 0x92, 0xa7, 0x40, 0x3c, 0xee, 0x19, 0xb1,
	    0x67, 0x54, 0x88, 0xda, 0x2f, 0x03, 0x75, 0xbc, 0xe1, 0x9a,
	};
}

bool HasFrameCryptorState(const E2EEManager* manager, TrackKind kind,
                          FrameCryptorDirection direction, FrameCryptorState state) {
	if (manager == nullptr) {
		return false;
	}
	for (const auto& cryptor : manager->FrameCryptors()) {
		if (cryptor.kind == kind && cryptor.direction == direction && cryptor.state == state &&
		    cryptor.enabled) {
			return true;
		}
	}
	return false;
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

	void OnAudioFrame(RemoteTrackInterface* track, RemoteParticipantInterface*,
	                  const AudioFrame& frame) override {
		if (!frame.data.empty() && frame.sample_rate > 0 && frame.num_channels > 0) {
			audio_frames_.fetch_add(1);
			if (track != nullptr) {
				std::lock_guard<std::mutex> guard(lock_);
				++media_frames_by_name_[track->Name()];
			}
		}
	}

	void OnVideoFrame(RemoteTrackInterface* track, RemoteParticipantInterface*,
	                  const VideoFrame& frame) override {
		last_video_width_.store(frame.width);
		last_video_height_.store(frame.height);
		const auto expected_size = static_cast<std::size_t>(frame.width) * frame.height * 3 / 2;
		if (frame.width > 0 && frame.height > 0 && frame.data.size() == expected_size) {
			video_frames_.fetch_add(1);
			if (track != nullptr) {
				std::lock_guard<std::mutex> guard(lock_);
				const auto name = track->Name();
				++media_frames_by_name_[name];
				video_dimensions_by_name_[name] = {frame.width, frame.height};
			}
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

	void OnEncryptionStateChanged(const EncryptionStateEvent& event) override {
		std::lock_guard<std::mutex> guard(lock_);
		encryption_states_[{event.cryptor.direction, event.cryptor.track_id}] = event.cryptor.state;
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
	bool encryption_state(const std::string& track_sid, FrameCryptorDirection direction,
	                      FrameCryptorState state) {
		std::lock_guard<std::mutex> guard(lock_);
		const auto found = encryption_states_.find({direction, track_sid});
		return found != encryption_states_.end() && found->second == state;
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
	uint64_t media_frames(const std::string& track_name) {
		std::lock_guard<std::mutex> guard(lock_);
		const auto found = media_frames_by_name_.find(track_name);
		return found != media_frames_by_name_.end() ? found->second : 0;
	}
	TrackDimensions video_dimensions(const std::string& track_name) {
		std::lock_guard<std::mutex> guard(lock_);
		const auto found = video_dimensions_by_name_.find(track_name);
		return found != video_dimensions_by_name_.end() ? found->second : TrackDimensions{};
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
	std::unordered_map<std::string, uint64_t> media_frames_by_name_;
	std::unordered_map<std::string, TrackDimensions> video_dimensions_by_name_;
	std::map<std::pair<FrameCryptorDirection, std::string>, FrameCryptorState> encryption_states_;
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

class DataTrackEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnReconnecting() override { reconnecting_.fetch_add(1); }
	void OnReconnected() override { reconnected_.fetch_add(1); }
	void OnDataTrackPublished(RemoteDataTrackInterface*, RemoteParticipantInterface*) override {
		remote_published_.fetch_add(1);
	}
	void OnDataTrackUnpublished(DataTrackInterface*, RemoteParticipantInterface*) override {
		remote_unpublished_.fetch_add(1);
	}
	void OnLocalDataTrackPublished(LocalDataTrackInterface*, ParticipantInterface*) override {
		local_published_.fetch_add(1);
	}
	void OnLocalDataTrackUnpublished(LocalDataTrackInterface*, ParticipantInterface*) override {
		local_unpublished_.fetch_add(1);
	}
	void OnDataTrackFrame(RemoteDataTrackInterface*, RemoteParticipantInterface*,
	                      const DataTrackFrame&) override {
		frames_.fetch_add(1);
	}

	uint32_t remote_published() const { return remote_published_.load(); }
	uint32_t remote_unpublished() const { return remote_unpublished_.load(); }
	uint32_t local_published() const { return local_published_.load(); }
	uint32_t local_unpublished() const { return local_unpublished_.load(); }
	uint32_t frames() const { return frames_.load(); }
	uint32_t reconnecting() const { return reconnecting_.load(); }
	uint32_t reconnected() const { return reconnected_.load(); }

private:
	std::atomic<uint32_t> remote_published_{0};
	std::atomic<uint32_t> remote_unpublished_{0};
	std::atomic<uint32_t> local_published_{0};
	std::atomic<uint32_t> local_unpublished_{0};
	std::atomic<uint32_t> frames_{0};
	std::atomic<uint32_t> reconnecting_{0};
	std::atomic<uint32_t> reconnected_{0};
};

class ReconnectEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnReconnecting() override { reconnecting_.fetch_add(1); }
	void OnReconnected() override { reconnected_.fetch_add(1); }
	void OnDisconnected() override { disconnected_.store(true); }

	bool reconnecting() const { return reconnecting_.load() > 0; }
	bool reconnected() const { return reconnected_.load() > 0; }
	bool disconnected() const { return disconnected_.load(); }
	uint32_t reconnecting_count() const { return reconnecting_.load(); }
	uint32_t reconnected_count() const { return reconnected_.load(); }

private:
	std::atomic<uint32_t> reconnecting_{0};
	std::atomic<uint32_t> reconnected_{0};
	std::atomic<bool> disconnected_{false};
};

class RecordingReconnectPolicy final : public ReconnectPolicy {
public:
	std::optional<std::chrono::milliseconds>
	NextRetryDelay(const ReconnectContext& context) override {
		calls_.fetch_add(1);
		last_retry_count_.store(context.retry_count);
		last_reason_.store(context.reason);
		return default_policy_.NextRetryDelay(context);
	}

	uint32_t calls() const { return calls_.load(); }
	uint32_t last_retry_count() const { return last_retry_count_.load(); }
	ReconnectReason last_reason() const { return last_reason_.load(); }

private:
	DefaultReconnectPolicy default_policy_;
	std::atomic<uint32_t> calls_{0};
	std::atomic<uint32_t> last_retry_count_{0};
	std::atomic<ReconnectReason> last_reason_{ReconnectReason::Unknown};
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
	EXPECT_EQ(events.reconnecting_count(), 1u);
	EXPECT_EQ(events.reconnected_count(), 1u);
	EXPECT_FALSE(room->Sid().empty());
	EXPECT_FALSE(room->GetLocalParticipant()->Sid().empty());

	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
}

TEST(LiveKitServerTest, RecoversAfterExplicitServerRestart) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN_RESTART");
	if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0' ||
	    std::getenv("LIVEKIT_SERVER_RESTART_READY_FILE") == nullptr) {
		GTEST_SKIP() << "Use run_reconnect_matrix.ps1 to run the destructive server-restart test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto reconnect_policy = std::make_shared<RecordingReconnectPolicy>();
	RoomOptions options;
	options.join_retries = 10;
	options.reconnect_timeout = std::chrono::seconds(5);
	options.reconnect_policy = reconnect_policy;
	auto room = CreateRoomUnique(options);
	ASSERT_NE(room, nullptr);
	ReconnectEvents events;
	room->AddEventListener(&events);
	ASSERT_TRUE(room->Connect(url, token, options));
	ASSERT_TRUE(WaitUntil([&] { return room->IsConnected(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(NotifyExternalHarness("LIVEKIT_SERVER_RESTART_READY_FILE"));

	ASSERT_TRUE(WaitUntil([&] { return events.reconnecting(); }, std::chrono::seconds(20)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected() && room->IsConnected(); },
	                      std::chrono::seconds(60)));
	EXPECT_FALSE(events.disconnected());
	EXPECT_GE(reconnect_policy->calls(), 1u);
	EXPECT_NE(reconnect_policy->last_reason(), ReconnectReason::MediaFailure);
	const std::vector<uint8_t> data{8, 6, 7, 5, 3, 0, 9};
	EXPECT_TRUE(room->GetLocalParticipant()->PublishData(data));

	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
}

TEST(LiveKitServerTest, UsesRefreshedTokenForResumeAndFullReconnect) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN_REFRESH");
	if (url == nullptr || token == nullptr || *url == '\0' || *token == '\0' ||
	    std::getenv("LIVEKIT_TOKEN_REFRESH_READY_FILE") == nullptr) {
		GTEST_SKIP() << "Use run_reconnect_matrix.ps1 to run the token-refresh test";
	}

	CApiLogCapture logs;
	CApiLogCallbackGuard log_callback_guard;
	lk_log_options_t log_options;
	lk_log_options_init(&log_options);
	log_options.livekit_level = LK_LOG_LEVEL_TRACE;
	log_options.webrtc_level = LK_LOG_LEVEL_INFO;
	log_options.websocket_level = LK_LOG_LEVEL_INFO;
	ASSERT_EQ(lk_log_set_options(&log_options), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_log_set_callback(CaptureCApiLog, &logs), LK_STATUS_OK) << lk_last_error();

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	auto reconnect_policy = std::make_shared<RecordingReconnectPolicy>();
	RoomOptions options;
	options.reconnect_policy = reconnect_policy;
	auto room = CreateRoomUnique(options);
	ASSERT_NE(room, nullptr);
	ReconnectEvents events;
	room->AddEventListener(&events);
	ASSERT_TRUE(room->Connect(url, token, options));
	ASSERT_TRUE(WaitUntil([&] { return room->IsConnected(); }, std::chrono::seconds(10)));
	auto* concrete_room = dynamic_cast<Room*>(room.get());
	ASSERT_NE(concrete_room, nullptr);
	ASSERT_TRUE(
	    WaitUntil([&] { return concrete_room->AccessTokenForReconnectForTesting() != token; },
	              std::chrono::seconds(10)));
	const auto refreshed_token = concrete_room->AccessTokenForReconnectForTesting();
	ASSERT_FALSE(refreshed_token.empty());
	ASSERT_TRUE(NotifyExternalHarness("LIVEKIT_TOKEN_REFRESH_READY_FILE"));

	ASSERT_TRUE(concrete_room->SimulateSignalDisconnectForTesting());
	ASSERT_TRUE(
	    WaitUntil([&] { return events.reconnecting_count() >= 1; }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected_count() >= 1 && room->IsConnected(); },
	                      std::chrono::seconds(30)));
	EXPECT_FALSE(concrete_room->AccessTokenForReconnectForTesting().empty());
	EXPECT_NE(concrete_room->AccessTokenForReconnectForTesting(), token);

	ASSERT_TRUE(concrete_room->SimulateFullReconnectForTesting());
	ASSERT_TRUE(
	    WaitUntil([&] { return events.reconnecting_count() >= 2; }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected_count() >= 2 && room->IsConnected(); },
	                      std::chrono::seconds(30)));
	EXPECT_FALSE(concrete_room->AccessTokenForReconnectForTesting().empty());
	EXPECT_NE(concrete_room->AccessTokenForReconnectForTesting(), token);
	EXPECT_GE(reconnect_policy->calls(), 1u);

	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
	EXPECT_TRUE(logs.Contains(LK_LOG_SOURCE_LIVEKIT, "WebSocket connection established"));
	EXPECT_TRUE(logs.Contains(LK_LOG_SOURCE_LIVEKIT, "connection recovery started"));
	EXPECT_TRUE(logs.Contains(LK_LOG_SOURCE_LIVEKIT, "connection recovery completed"));
	EXPECT_TRUE(logs.ContainsSource(LK_LOG_SOURCE_WEBRTC));
	EXPECT_TRUE(logs.ContainsSource(LK_LOG_SOURCE_WEBSOCKET));
	EXPECT_FALSE(logs.ContainsValue(token));
	EXPECT_FALSE(logs.ContainsValue(refreshed_token));
	EXPECT_FALSE(logs.ContainsValue("access_token="));
	EXPECT_FALSE(logs.ContainsValue("candidate:"));
	EXPECT_FALSE(logs.ContainsValue("a=candidate"));
	EXPECT_FALSE(logs.ContainsValue("v=0\r\n"));
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
	auto reconnect_policy = std::make_shared<RecordingReconnectPolicy>();
	RoomOptions sender_options;
	sender_options.reconnect_policy = reconnect_policy;
	auto sender = CreateRoomUnique(sender_options);
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token, sender_options));
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
	ASSERT_TRUE(concrete_sender->SimulateMediaFailureForTesting());
	ASSERT_TRUE(WaitUntil([&] { return events.reconnecting(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected() && sender->IsConnected(); },
	                      std::chrono::seconds(30)));
	EXPECT_EQ(reconnect_policy->calls(), 1u);
	EXPECT_EQ(reconnect_policy->last_retry_count(), 0u);
	EXPECT_EQ(reconnect_policy->last_reason(), ReconnectReason::MediaFailure);
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

TEST(LiveKitServerTest, PublishesAndReceivesSelectedVideoCodec) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the codec "
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

	VideoFrame video_frame;
	video_frame.width = 640;
	video_frame.height = 360;
	video_frame.data.resize(video_frame.width * video_frame.height * 3 / 2, 128);
	std::fill(video_frame.data.begin(),
	          video_frame.data.begin() + video_frame.width * video_frame.height, 72);
	video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                               std::chrono::steady_clock::now().time_since_epoch())
	                               .count();
	auto video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(video_source->CaptureFrame(video_frame));
	auto video_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "integration-selected-codec-video", video_source.get());
	ASSERT_NE(video_track, nullptr);
	TrackPublishOptions video_options;
	video_options.source = TrackSource::Camera;
	video_options.simulcast = true;
	std::string expected_video_mime_type;
	video_options.video_codec = VideoCodecFromEnvironment(expected_video_mime_type);
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(video_track.get(), video_options));
	auto* concrete_video_track = dynamic_cast<LocalVideoTrack*>(video_track.get());
	ASSERT_NE(concrete_video_track, nullptr);
	ASSERT_NE(concrete_video_track->Transceiver(), nullptr);
	ASSERT_NE(concrete_video_track->Transceiver()->sender(), nullptr);
	const auto initial_parameters = concrete_video_track->Transceiver()->sender()->GetParameters();
	ASSERT_FALSE(initial_parameters.encodings.empty());
	ASSERT_TRUE(
	    sender->GetLocalParticipant()->UpdateVideoEncoding(video_track.get(), {900000, 12}));
	const auto updated_parameters = concrete_video_track->Transceiver()->sender()->GetParameters();
	ASSERT_EQ(updated_parameters.encodings.size(), initial_parameters.encodings.size());
	for (std::size_t index = 0; index < updated_parameters.encodings.size(); ++index) {
		EXPECT_EQ(updated_parameters.encodings[index].active,
		          initial_parameters.encodings[index].active);
		EXPECT_EQ(updated_parameters.encodings[index].rid, initial_parameters.encodings[index].rid);
		EXPECT_EQ(updated_parameters.encodings[index].scale_resolution_down_by,
		          initial_parameters.encodings[index].scale_resolution_down_by);
		EXPECT_EQ(updated_parameters.encodings[index].scalability_mode,
		          initial_parameters.encodings[index].scalability_mode);
		ASSERT_TRUE(updated_parameters.encodings[index].max_framerate.has_value());
		EXPECT_DOUBLE_EQ(*updated_parameters.encodings[index].max_framerate, 12.0);
	}
	ASSERT_TRUE(updated_parameters.encodings.back().max_bitrate_bps.has_value());
	EXPECT_EQ(*updated_parameters.encodings.back().max_bitrate_bps, 900000);

	ASSERT_TRUE(sender->GetLocalParticipant()->UpdateVideoEncoding(video_track.get(), {}));
	const auto reset_parameters = concrete_video_track->Transceiver()->sender()->GetParameters();
	ASSERT_EQ(reset_parameters.encodings.size(), initial_parameters.encodings.size());
	for (std::size_t index = 0; index < reset_parameters.encodings.size(); ++index) {
		EXPECT_EQ(reset_parameters.encodings[index].max_bitrate_bps,
		          initial_parameters.encodings[index].max_bitrate_bps);
		EXPECT_EQ(reset_parameters.encodings[index].max_framerate,
		          initial_parameters.encodings[index].max_framerate);
	}

	const auto video_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!events.video_received() && std::chrono::steady_clock::now() < video_deadline) {
		video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                               std::chrono::steady_clock::now().time_since_epoch())
		                               .count();
		ASSERT_TRUE(video_source->CaptureFrame(video_frame));
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	ASSERT_TRUE(events.video_received());

	auto* remote_sender = receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
	ASSERT_NE(remote_sender, nullptr);
	auto* video_publication = remote_sender->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(video_publication, nullptr);
	EXPECT_EQ(video_publication->MimeType(), expected_video_mime_type);
	const bool expect_simulcast = video_options.video_codec == VideoCodec::VP8 ||
	                              video_options.video_codec == VideoCodec::H264;
	EXPECT_EQ(video_publication->IsSimulcasted(), expect_simulcast);

	receiver->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
	video_track.reset();
	video_source.reset();
}

TEST(LiveKitServerTest, PublishesBackupCodecWhenRequestedByServer) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the backup "
		                "codec integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	RoomOptions sender_options;
	sender_options.dynacast = true;
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token, sender_options));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));

	VideoFrame frame;
	frame.width = 640;
	frame.height = 360;
	frame.data.resize(frame.width * frame.height * 3 / 2, 128);
	std::fill(frame.data.begin(), frame.data.begin() + frame.width * frame.height, 80);
	frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                         std::chrono::steady_clock::now().time_since_epoch())
	                         .count();
	auto source = CreateVideoSourceUnique();
	ASSERT_TRUE(source->CaptureFrame(frame));
	auto track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "integration-backup-codec-video", source.get());
	ASSERT_NE(track, nullptr);
	TrackPublishOptions options;
	options.source = TrackSource::Camera;
	options.video_codec = VideoCodec::VP9;
	options.backup_video_codec = VideoCodec::VP8;
	options.backup_codec_policy = BackupCodecPolicy::PreferRegression;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(track.get(), options));
	ASSERT_TRUE(
	    sender->GetLocalParticipant()->UpdateVideoEncoding(track.get(), {320000, 15}, true));

	auto* concrete_participant = dynamic_cast<LocalParticipant*>(sender->GetLocalParticipant());
	auto* concrete_track = dynamic_cast<LocalVideoTrack*>(track.get());
	ASSERT_NE(concrete_participant, nullptr);
	ASSERT_NE(concrete_track, nullptr);
	EXPECT_TRUE(concrete_track->AdditionalCodecs().empty());

	SubscribedQualityUpdate request;
	request.track_sid = track->Sid();
	request.codecs = {
	    {"video/VP9", {{VideoQuality::High, true}}},
	    {"video/VP8",
	     {{VideoQuality::Low, true}, {VideoQuality::Medium, true}, {VideoQuality::High, true}}},
	};
	concrete_participant->SubscribedQualityUpdate(std::move(request));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    const auto codecs = concrete_track->AdditionalCodecs();
		    return codecs.size() == 1 && codecs[0].codec == VideoCodec::VP8 &&
		           codecs[0].transceiver != nullptr;
	    },
	    std::chrono::seconds(10)));
	const auto additional_codecs = concrete_track->AdditionalCodecs();
	ASSERT_EQ(additional_codecs.size(), 1u);
	ASSERT_NE(additional_codecs[0].transceiver, nullptr);
	ASSERT_NE(additional_codecs[0].transceiver->sender(), nullptr);
	const auto backup_parameters = additional_codecs[0].transceiver->sender()->GetParameters();
	ASSERT_FALSE(backup_parameters.encodings.empty());
	ASSERT_TRUE(backup_parameters.encodings.back().max_bitrate_bps.has_value());
	EXPECT_EQ(*backup_parameters.encodings.back().max_bitrate_bps, 320000);
	for (const auto& encoding : backup_parameters.encodings) {
		ASSERT_TRUE(encoding.max_framerate.has_value());
		EXPECT_DOUBLE_EQ(*encoding.max_framerate, 15.0);
	}

	ASSERT_TRUE(WaitUntil(
	    [&] {
		    auto* remote_sender =
		        receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
		    auto* publication = remote_sender != nullptr
		                            ? remote_sender->GetTrackPublication(TrackSource::Camera)
		                            : nullptr;
		    auto* concrete_publication = dynamic_cast<TrackPublication*>(publication);
		    if (concrete_publication == nullptr) {
			    return false;
		    }
		    const auto info = concrete_publication->Info();
		    return std::any_of(info.codecs().begin(), info.codecs().end(),
		                       [](const auto& codec) { return codec.mime_type() == "video/VP8"; });
	    },
	    std::chrono::seconds(10)));

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(track.get(), false));
	EXPECT_TRUE(concrete_track->AdditionalCodecs().empty());
	receiver->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
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
	RoomOptions sender_options;
	sender_options.dynacast = true;
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token));
	ASSERT_TRUE(sender->Connect(url, sender_token, sender_options));
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
	ASSERT_NE(audio_publication->Track(), nullptr);
	EXPECT_EQ(audio_publication->Track()->Name(), "integration-audio");
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
	video_frame.width = 640;
	video_frame.height = 360;
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
	video_options.simulcast = true;
	std::string expected_video_mime_type;
	video_options.video_codec = VideoCodecFromEnvironment(expected_video_mime_type);
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
	std::string outbound_stats;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    outbound_stats = video_track->GetRTCStats();
		    return outbound_stats.find("outbound-rtp") != std::string::npos;
	    },
	    std::chrono::seconds(10)))
	    << outbound_stats;
	const auto outbound_snapshot = video_track->GetRTCStatsSnapshot();
	EXPECT_TRUE(std::any_of(
	    outbound_snapshot.streams.begin(), outbound_snapshot.streams.end(), [](const auto& stats) {
		    return stats.direction == RTCStatsDirection::Send && stats.bytes > 0;
	    }));
	ASSERT_TRUE(WaitUntil(
	    [&] { return sender_participant->GetTrackPublication(TrackSource::Camera) != nullptr; },
	    std::chrono::seconds(10)))
	    << PublicationSummary(sender_participant);
	auto* video_publication = sender_participant->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(video_publication, nullptr);
	ASSERT_NE(video_publication->Track(), nullptr);
	std::string inbound_stats;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    inbound_stats = video_publication->Track()->GetRTCStats();
		    return inbound_stats.find("inbound-rtp") != std::string::npos;
	    },
	    std::chrono::seconds(10)))
	    << inbound_stats;
	const auto inbound_snapshot = video_publication->Track()->GetRTCStatsSnapshot();
	EXPECT_TRUE(std::any_of(
	    inbound_snapshot.streams.begin(), inbound_snapshot.streams.end(), [](const auto& stats) {
		    return stats.direction == RTCStatsDirection::Receive && stats.bytes > 0;
	    }));
	EXPECT_EQ(video_publication->MimeType(), expected_video_mime_type);
	const bool expect_simulcast = video_options.video_codec == VideoCodec::VP8 ||
	                              video_options.video_codec == VideoCodec::H264;
	EXPECT_EQ(video_publication->IsSimulcasted(), expect_simulcast);
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
	ASSERT_TRUE(WaitUntil([&] {
		return video_publication->SubscriptionStatus() == TrackSubscriptionStatus::Unsubscribed &&
		       video_publication->Track() == nullptr &&
		       events.subscription_status(video_publication->Sid(),
		                                  TrackSubscriptionStatus::Unsubscribed);
	}));
	ASSERT_TRUE(receiver->SetRemoteTrackSubscribed(sender_participant->Sid(),
	                                               video_publication->Sid(), true));
	ASSERT_TRUE(WaitUntil([&] {
		return video_publication->SubscriptionStatus() == TrackSubscriptionStatus::Subscribed &&
		       video_publication->Track() != nullptr &&
		       events.subscription_status(video_publication->Sid(),
		                                  TrackSubscriptionStatus::Subscribed);
	}));
	EXPECT_EQ(video_publication->Name(), "integration-video");
	EXPECT_EQ(video_publication->Kind(), TrackKind::Video);
	EXPECT_EQ(video_publication->Track()->Name(), "integration-video");
	EXPECT_TRUE(sender_participant->IsCameraEnabled());
	const auto participant_snapshots = receiver->GetRemoteParticipantSnapshots();
	const auto participant_snapshot =
	    std::find_if(participant_snapshots.begin(), participant_snapshots.end(),
	                 [&](const auto& item) { return item.sid == sender_participant->Sid(); });
	ASSERT_NE(participant_snapshot, participant_snapshots.end());
	const auto publication_snapshot = std::find_if(
	    participant_snapshot->publications.begin(), participant_snapshot->publications.end(),
	    [&](const auto& item) { return item.sid == video_publication->Sid(); });
	ASSERT_NE(publication_snapshot, participant_snapshot->publications.end());
	EXPECT_EQ(publication_snapshot->subscription_status, TrackSubscriptionStatus::Subscribed);
	EXPECT_EQ(publication_snapshot->dimensions.width, 640u);
	EXPECT_EQ(publication_snapshot->dimensions.height, 360u);
	ASSERT_TRUE(publication_snapshot->subscribed_track.has_value());
	EXPECT_EQ(publication_snapshot->subscribed_track->sid, video_publication->Track()->Sid());
	EXPECT_EQ(publication_snapshot->subscribed_track->kind, TrackKind::Video);
	EXPECT_EQ(publication_snapshot->subscribed_track->source, TrackSource::Camera);
	EXPECT_EQ(publication_snapshot->subscribed_track->dimensions.width, 640u);

	// Screen-share helpers must override a caller-provided source and advertise the correct
	// LiveKit source without taking ownership of desktop capture.
	auto screen_video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(screen_video_source->CaptureFrame(video_frame));
	auto screen_video_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "integration-screen-video", screen_video_source.get());
	ASSERT_NE(screen_video_track, nullptr);
	TrackPublishOptions screen_video_options;
	screen_video_options.source = TrackSource::Camera;
	screen_video_options.video_codec = video_options.video_codec;
	screen_video_options.simulcast = false;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishScreenShareVideoTrack(
	    screen_video_track.get(), screen_video_options));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                                   std::chrono::steady_clock::now().time_since_epoch())
		                                   .count();
		    return screen_video_source->CaptureFrame(video_frame) &&
		           events.media_frames("integration-screen-video") >= 3;
	    },
	    std::chrono::seconds(10)));
	auto* screen_video_publication =
	    sender_participant->GetTrackPublication(TrackSource::ScreenShare);
	ASSERT_NE(screen_video_publication, nullptr);
	EXPECT_EQ(screen_video_publication->Name(), "integration-screen-video");
	EXPECT_EQ(screen_video_publication->Kind(), TrackKind::Video);
	EXPECT_EQ(screen_video_publication->MimeType(), expected_video_mime_type);
	EXPECT_TRUE(sender_participant->IsScreenShareEnabled());
	ASSERT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(screen_video_track.get()));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return sender_participant->GetTrackPublication(TrackSource::ScreenShare) == nullptr;
	    },
	    std::chrono::seconds(10)));

	auto screen_audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto screen_audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "integration-screen-audio", screen_audio_source.get());
	ASSERT_NE(screen_audio_track, nullptr);
	TrackPublishOptions screen_audio_options;
	screen_audio_options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishScreenShareAudioTrack(
	    screen_audio_track.get(), screen_audio_options));
	const auto screen_audio_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (sender_participant->GetTrackPublication(TrackSource::ScreenShareAudio) == nullptr &&
	       std::chrono::steady_clock::now() < screen_audio_deadline) {
		ASSERT_TRUE(screen_audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	auto* screen_audio_publication =
	    sender_participant->GetTrackPublication(TrackSource::ScreenShareAudio);
	ASSERT_NE(screen_audio_publication, nullptr);
	EXPECT_EQ(screen_audio_publication->Name(), "integration-screen-audio");
	EXPECT_EQ(screen_audio_publication->Kind(), TrackKind::Audio);
	ASSERT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(screen_audio_track.get()));
	ASSERT_TRUE(WaitUntil([&] {
		return sender_participant->GetTrackPublication(TrackSource::ScreenShareAudio) == nullptr;
	}));

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
	screen_video_track.reset();
	screen_video_source.reset();
	screen_audio_track.reset();
	screen_audio_source.reset();
	audio_track.reset();
	audio_source.reset();
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, PublishesReadsAndUnpublishesEncryptedDataTrack) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the data "
		                "track integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	E2eeOptions e2ee;
	e2ee.encryption_type = EncryptionType::Gcm;
	e2ee.shared_key = IntegrationE2eeKey();
	RoomOptions room_options;
	room_options.e2ee = e2ee;
	auto receiver = CreateRoomUnique(room_options);
	auto sender = CreateRoomUnique(room_options);
	DataTrackEvents receiver_events;
	DataTrackEvents sender_events;
	receiver->AddEventListener(&receiver_events);
	sender->AddEventListener(&sender_events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token, room_options));
	ASSERT_TRUE(sender->Connect(url, sender_token, room_options));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));

	DataTrackSchema schema;
	schema.id = {"integration.telemetry.v1", {DataTrackSchemaEncodingKind::JsonSchema, {}}};
	const std::string schema_json =
	    R"({"type":"object","properties":{"x":{"type":"number"}},"required":["x"]})";
	schema.definition.assign(schema_json.begin(), schema_json.end());
	const auto store_schema_error = sender->StoreDataTrackSchema(schema);
	ASSERT_FALSE(store_schema_error) << store_schema_error.message;
	DataTrackSchema oversized = schema;
	oversized.id.name = "integration.oversized";
	oversized.definition.resize(kMaximumDataTrackSchemaDefinitionSize + 1);
	EXPECT_EQ(sender->StoreDataTrackSchema(std::move(oversized)).code,
	          DataTrackErrorCode::InvalidSchema);

	DataTrackPublishOptions publish_options;
	publish_options.name = "integration-telemetry";
	publish_options.frame_encoding = DataTrackFrameEncoding{DataTrackFrameEncodingKind::Json, {}};
	publish_options.schema = schema.id;
	auto published = sender->GetLocalParticipant()->PublishDataTrack(publish_options);
	ASSERT_TRUE(published) << published.error.message;
	ASSERT_NE(published.track, nullptr);
	EXPECT_TRUE(published.track->Info().uses_e2ee);
	EXPECT_EQ(sender_events.local_published(), 1u);

	RemoteParticipantInterface* remote_sender = nullptr;
	RemoteDataTrackInterface* remote_track = nullptr;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    remote_sender =
		        receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
		    remote_track = remote_sender != nullptr
		                       ? dynamic_cast<RemoteDataTrackInterface*>(
		                             remote_sender->GetDataTrackByName(publish_options.name))
		                       : nullptr;
		    return remote_track != nullptr && receiver_events.remote_published() >= 1;
	    },
	    std::chrono::seconds(10)));
	EXPECT_TRUE(remote_track->Info().uses_e2ee);
	ASSERT_TRUE(remote_track->Info().schema.has_value());
	EXPECT_EQ(*remote_track->Info().schema, schema.id);
	auto remote_schema =
	    receiver->GetDataTrackSchema(sender->GetLocalParticipant()->Identity(), schema.id);
	ASSERT_TRUE(remote_schema) << remote_schema.error.message;
	EXPECT_EQ(*remote_schema.schema, schema);
	// The second lookup is served from the room cache and remains available during recovery.
	EXPECT_TRUE(receiver->GetDataTrackSchema(sender->GetLocalParticipant()->Identity(), schema.id));
	auto missing_schema_id = schema.id;
	missing_schema_id.name = "integration.missing";
	const auto missing_schema = receiver->GetDataTrackSchema(
	    sender->GetLocalParticipant()->Identity(), std::move(missing_schema_id));
	EXPECT_FALSE(missing_schema);
	EXPECT_EQ(missing_schema.error.code, DataTrackErrorCode::NotFound);
	DataTrackSubscriptionOptions subscription_options;
	subscription_options.buffer_capacity = 4;
	subscription_options.max_partial_frames = 2;
	auto reader = remote_track->Subscribe(subscription_options);
	ASSERT_NE(reader, nullptr);
	auto* original_remote_track = remote_track;
	const auto original_remote_sid = remote_track->Info().sid;

	DataTrackFrame small{{'{', '"', 'x', '"', ':', '1', '}'}, 123456};
	DataTrackFrame received;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    const auto error = published.track->TryPush(small);
		    return !error && reader->TryRead(received);
	    },
	    std::chrono::seconds(10)));
	EXPECT_EQ(received.payload, small.payload);
	EXPECT_EQ(received.user_timestamp, small.user_timestamp);
	while (reader->TryRead(received)) {
	}

	DataTrackFrame fragmented;
	fragmented.payload.resize(40'000);
	for (std::size_t index = 0; index < fragmented.payload.size(); ++index) {
		fragmented.payload[index] = static_cast<uint8_t>(index % 251);
	}
	fragmented.user_timestamp = 987654321;
	ASSERT_FALSE(published.track->TryPush(fragmented));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return reader->ReadFor(received, std::chrono::milliseconds(100)) &&
		           received.user_timestamp == fragmented.user_timestamp;
	    },
	    std::chrono::seconds(10)));
	EXPECT_EQ(received.payload, fragmented.payload);
	EXPECT_EQ(received.user_timestamp, fragmented.user_timestamp);
	EXPECT_GE(receiver_events.frames(), 2u);

	auto* concrete_sender = dynamic_cast<Room*>(sender.get());
	ASSERT_NE(concrete_sender, nullptr);
	ASSERT_TRUE(concrete_sender->SimulateMediaFailureForTesting());
	ASSERT_TRUE(
	    WaitUntil([&] { return sender_events.reconnecting() >= 1; }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return sender_events.reconnected() >= 1 && sender->IsConnected(); },
	                      std::chrono::seconds(30)));
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    remote_sender =
		        receiver->GetRemoteParticipantByIdentity(sender->GetLocalParticipant()->Identity());
		    remote_track = remote_sender != nullptr
		                       ? dynamic_cast<RemoteDataTrackInterface*>(
		                             remote_sender->GetDataTrackByName(publish_options.name))
		                       : nullptr;
		    return remote_track != nullptr && remote_track->IsPublished() &&
		           remote_track->Info().sid != original_remote_sid;
	    },
	    std::chrono::seconds(10)));
	if (remote_track == original_remote_track) {
		EXPECT_FALSE(reader->IsClosed());
	} else {
		reader = remote_track->Subscribe(subscription_options);
		ASSERT_NE(reader, nullptr);
	}
	DataTrackFrame after_reconnect{{9, 8, 7, 6}, 444};
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    const auto error = published.track->TryPush(after_reconnect);
		    return !error && reader->TryRead(received) &&
		           received.user_timestamp == after_reconnect.user_timestamp;
	    },
	    std::chrono::seconds(10)));
	EXPECT_EQ(received.payload, after_reconnect.payload);

	const auto remote_sid = remote_track->Info().sid;
	ASSERT_FALSE(published.track->Unpublish());
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return reader->IsClosed() && sender_events.local_unpublished() >= 1 &&
		           receiver_events.remote_unpublished() >= 1;
	    },
	    std::chrono::seconds(10)));
	EXPECT_EQ(remote_sender->GetDataTrackBySid(remote_sid), nullptr);
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, CApiPublishesReadsAndUnpublishesDataTrack) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the C API "
		                "DataTrack integration test";
	}

	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();
	auto room_deleter = [](lk_room_t* room) { lk_room_destroy(room); };
	auto local_track_deleter = [](lk_local_data_track_t* track) {
		if (track != nullptr && lk_local_data_track_is_published(track)) {
			(void)lk_local_data_track_unpublish(track);
		}
		(void)lk_local_data_track_destroy(track);
	};
	auto reader_deleter = [](lk_data_track_reader_t* reader) {
		lk_data_track_reader_destroy(reader);
	};
	auto frame_deleter = [](lk_data_track_frame_t* frame) { lk_data_track_frame_destroy(frame); };
	auto schema_deleter = [](lk_data_track_schema_t* schema) {
		lk_data_track_schema_destroy(schema);
	};
	auto data_track_snapshot_deleter = [](lk_remote_data_track_list_t* snapshot) {
		lk_remote_data_track_list_destroy(snapshot);
	};

	lk_room_t* receiver_handle = nullptr;
	lk_room_t* sender_handle = nullptr;
	ASSERT_EQ(lk_room_create(&receiver_handle), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_create(&sender_handle), LK_STATUS_OK) << lk_last_error();
	std::unique_ptr<lk_room_t, decltype(room_deleter)> receiver(receiver_handle, room_deleter);
	std::unique_ptr<lk_room_t, decltype(room_deleter)> sender(sender_handle, room_deleter);

	CApiDataTrackEvents events;
	lk_room_callbacks_t receiver_callbacks;
	lk_room_callbacks_init(&receiver_callbacks);
	receiver_callbacks.user_data = &events;
	receiver_callbacks.on_data_track_published = OnCApiRemoteDataTrackPublished;
	receiver_callbacks.on_data_track_unpublished = OnCApiRemoteDataTrackUnpublished;
	receiver_callbacks.on_data_track_frame = OnCApiDataTrackFrame;
	ASSERT_EQ(lk_room_set_callbacks(receiver.get(), &receiver_callbacks), LK_STATUS_OK);
	lk_room_callbacks_t sender_callbacks;
	lk_room_callbacks_init(&sender_callbacks);
	sender_callbacks.user_data = &events;
	sender_callbacks.on_local_data_track_published = OnCApiLocalDataTrackPublished;
	sender_callbacks.on_local_data_track_unpublished = OnCApiLocalDataTrackUnpublished;
	ASSERT_EQ(lk_room_set_callbacks(sender.get(), &sender_callbacks), LK_STATUS_OK);

	ASSERT_EQ(lk_room_connect(receiver.get(), url, receiver_token), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_connect(sender.get(), url, sender_token), LK_STATUS_OK) << lk_last_error();
	ASSERT_TRUE(WaitUntil(
	    [&] { return lk_room_is_connected(receiver.get()) && lk_room_is_connected(sender.get()); },
	    std::chrono::seconds(10)));

	lk_data_track_schema_id_t schema_id;
	lk_data_track_schema_id_init(&schema_id);
	schema_id.name = "c.telemetry.v1";
	schema_id.encoding = LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA;
	const std::string schema_json =
	    R"({"type":"object","properties":{"x":{"type":"number"}},"required":["x"]})";
	ASSERT_EQ(lk_room_store_data_track_schema(sender.get(), &schema_id,
	                                          reinterpret_cast<const uint8_t*>(schema_json.data()),
	                                          schema_json.size()),
	          LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();

	lk_data_track_publish_options_t publish_options;
	lk_data_track_publish_options_init(&publish_options);
	publish_options.name = "c-api-telemetry";
	publish_options.has_frame_encoding = 1;
	publish_options.frame_encoding = LK_DATA_TRACK_FRAME_ENCODING_JSON;
	publish_options.has_schema = 1;
	publish_options.schema = schema_id;
	lk_local_data_track_t* local_track_handle = nullptr;
	ASSERT_EQ(lk_room_publish_data_track(sender.get(), &publish_options, &local_track_handle),
	          LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	std::unique_ptr<lk_local_data_track_t, decltype(local_track_deleter)> local_track(
	    local_track_handle, local_track_deleter);
	ASSERT_TRUE(events.local_published());
	ASSERT_TRUE(lk_local_data_track_is_published(local_track.get()));
	lk_data_track_snapshot_info_t local_info;
	lk_data_track_snapshot_info_init(&local_info);
	ASSERT_EQ(lk_local_data_track_info(local_track.get(), &local_info), LK_STATUS_OK);
	EXPECT_TRUE(local_info.is_published);
	EXPECT_TRUE(local_info.has_frame_encoding);
	EXPECT_EQ(local_info.frame_encoding, LK_DATA_TRACK_FRAME_ENCODING_JSON);
	EXPECT_TRUE(local_info.has_schema);
	EXPECT_EQ(local_info.schema_encoding, LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA);
	EXPECT_EQ(lk_local_data_track_destroy(local_track.get()), LK_DATA_TRACK_ERROR_NOT_ALLOWED);
	const auto local_name_size = lk_local_data_track_name(local_track.get(), nullptr, 0);
	std::vector<char> local_name(local_name_size);
	ASSERT_EQ(lk_local_data_track_name(local_track.get(), local_name.data(), local_name.size()),
	          local_name.size());
	EXPECT_STREQ(local_name.data(), "c-api-telemetry");

	std::string remote_sid;
	std::string publisher_identity;
	ASSERT_TRUE(WaitUntil([&] { return events.remote_published(remote_sid, publisher_identity); },
	                      std::chrono::seconds(10)));
	lk_remote_data_track_list_t* snapshot_handle = nullptr;
	ASSERT_EQ(lk_room_create_remote_data_track_snapshot(receiver.get(), &snapshot_handle),
	          LK_STATUS_OK)
	    << lk_last_error();
	std::unique_ptr<lk_remote_data_track_list_t, decltype(data_track_snapshot_deleter)> snapshot(
	    snapshot_handle, data_track_snapshot_deleter);
	ASSERT_EQ(lk_remote_data_track_list_count(snapshot.get()), 1u);
	const lk_remote_data_track_snapshot_t* remote_track_snapshot = nullptr;
	ASSERT_EQ(lk_remote_data_track_list_at(snapshot.get(), 0, &remote_track_snapshot),
	          LK_STATUS_OK);
	ASSERT_NE(remote_track_snapshot, nullptr);
	lk_data_track_snapshot_info_t remote_info;
	lk_data_track_snapshot_info_init(&remote_info);
	ASSERT_EQ(lk_remote_data_track_snapshot_info(remote_track_snapshot, &remote_info),
	          LK_STATUS_OK);
	EXPECT_TRUE(remote_info.is_published);
	EXPECT_FALSE(remote_info.uses_e2ee);
	EXPECT_EQ(remote_info.frame_encoding, LK_DATA_TRACK_FRAME_ENCODING_JSON);
	EXPECT_EQ(remote_info.schema_encoding, LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA);
	auto read_snapshot_string = [remote_track_snapshot](auto getter) {
		std::vector<char> value(getter(remote_track_snapshot, nullptr, 0));
		if (!value.empty()) {
			getter(remote_track_snapshot, value.data(), value.size());
		}
		return value.empty() ? std::string{} : std::string(value.data());
	};
	EXPECT_EQ(read_snapshot_string(lk_remote_data_track_snapshot_publisher_identity),
	          publisher_identity);
	EXPECT_EQ(read_snapshot_string(lk_remote_data_track_snapshot_sid), remote_sid);
	EXPECT_EQ(read_snapshot_string(lk_remote_data_track_snapshot_name), "c-api-telemetry");
	EXPECT_EQ(read_snapshot_string(lk_remote_data_track_snapshot_schema_name), "c.telemetry.v1");
	lk_data_track_subscription_options_t subscription_options;
	lk_data_track_subscription_options_init(&subscription_options);
	subscription_options.buffer_capacity = 4;
	subscription_options.max_partial_frames = 2;
	ASSERT_EQ(
	    lk_room_update_data_track_subscription_options(receiver.get(), publisher_identity.c_str(),
	                                                   remote_sid.c_str(), &subscription_options),
	    LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	lk_data_track_reader_t* reader_handle = nullptr;
	ASSERT_EQ(lk_room_subscribe_data_track(receiver.get(), publisher_identity.c_str(),
	                                       remote_sid.c_str(), &subscription_options,
	                                       &reader_handle),
	          LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	std::unique_ptr<lk_data_track_reader_t, decltype(reader_deleter)> reader(reader_handle,
	                                                                         reader_deleter);
	subscription_options.has_target_fps = 1;
	subscription_options.target_fps = 30;
	subscription_options.max_partial_frames = 3;
	ASSERT_EQ(
	    lk_room_update_data_track_subscription_options(receiver.get(), publisher_identity.c_str(),
	                                                   remote_sid.c_str(), &subscription_options),
	    LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	lk_data_track_frame_t* empty_frame = nullptr;
	EXPECT_EQ(lk_data_track_reader_try_read(reader.get(), &empty_frame), LK_DATA_TRACK_READ_EMPTY);
	EXPECT_EQ(empty_frame, nullptr);

	lk_data_track_schema_t* remote_schema_handle = nullptr;
	ASSERT_EQ(lk_room_get_data_track_schema(receiver.get(), publisher_identity.c_str(), &schema_id,
	                                        &remote_schema_handle),
	          LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	std::unique_ptr<lk_data_track_schema_t, decltype(schema_deleter)> remote_schema(
	    remote_schema_handle, schema_deleter);
	EXPECT_EQ(lk_data_track_schema_encoding(remote_schema.get()),
	          LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA);
	const auto remote_schema_name_size = lk_data_track_schema_name(remote_schema.get(), nullptr, 0);
	std::vector<char> remote_schema_name(remote_schema_name_size);
	ASSERT_EQ(lk_data_track_schema_name(remote_schema.get(), remote_schema_name.data(),
	                                    remote_schema_name.size()),
	          remote_schema_name.size());
	EXPECT_STREQ(remote_schema_name.data(), "c.telemetry.v1");
	std::vector<uint8_t> schema_definition(
	    lk_data_track_schema_definition(remote_schema.get(), nullptr, 0));
	ASSERT_EQ(lk_data_track_schema_definition(remote_schema.get(), schema_definition.data(),
	                                          schema_definition.size()),
	          schema_definition.size());
	EXPECT_EQ(std::string(schema_definition.begin(), schema_definition.end()), schema_json);

	const std::vector<uint8_t> first_payload{'{', '"', 'x', '"', ':', '1', '}'};
	lk_data_track_frame_t* received_handle = nullptr;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    if (lk_local_data_track_try_push(local_track.get(), first_payload.data(),
		                                     first_payload.size(), 1,
		                                     123456) != LK_DATA_TRACK_ERROR_NONE) {
			    return false;
		    }
		    return lk_data_track_reader_read_for(reader.get(), 100, &received_handle) ==
		           LK_DATA_TRACK_READ_FRAME;
	    },
	    std::chrono::seconds(10)));
	std::unique_ptr<lk_data_track_frame_t, decltype(frame_deleter)> received(received_handle,
	                                                                         frame_deleter);
	std::vector<uint8_t> received_payload(lk_data_track_frame_data(received.get(), nullptr, 0));
	ASSERT_EQ(
	    lk_data_track_frame_data(received.get(), received_payload.data(), received_payload.size()),
	    received_payload.size());
	EXPECT_EQ(received_payload, first_payload);
	EXPECT_TRUE(lk_data_track_frame_has_user_timestamp(received.get()));
	EXPECT_EQ(lk_data_track_frame_user_timestamp(received.get()), 123456u);
	ASSERT_TRUE(WaitUntil([&] { return events.frame_received(first_payload, 123456, 1); },
	                      std::chrono::seconds(10)));

	std::vector<uint8_t> fragmented(40'000);
	for (size_t index = 0; index < fragmented.size(); ++index) {
		fragmented[index] = static_cast<uint8_t>(index % 251);
	}
	received.reset();
	received_handle = nullptr;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    if (lk_local_data_track_try_push(local_track.get(), fragmented.data(),
		                                     fragmented.size(), 1,
		                                     987654321) != LK_DATA_TRACK_ERROR_NONE) {
			    return false;
		    }
		    return lk_data_track_reader_read_for(reader.get(), 100, &received_handle) ==
		           LK_DATA_TRACK_READ_FRAME;
	    },
	    std::chrono::seconds(10)))
	    << lk_last_error();
	received.reset(received_handle);
	received_payload.resize(lk_data_track_frame_data(received.get(), nullptr, 0));
	ASSERT_EQ(
	    lk_data_track_frame_data(received.get(), received_payload.data(), received_payload.size()),
	    received_payload.size());
	EXPECT_EQ(received_payload, fragmented);
	EXPECT_EQ(lk_data_track_frame_user_timestamp(received.get()), 987654321u);

	ASSERT_EQ(lk_local_data_track_unpublish(local_track.get()), LK_DATA_TRACK_ERROR_NONE)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.unpublished(); }, std::chrono::seconds(10)));
	EXPECT_TRUE(lk_data_track_reader_is_closed(reader.get()));
	EXPECT_FALSE(lk_local_data_track_is_published(local_track.get()));
	lk_data_track_snapshot_info_init(&local_info);
	ASSERT_EQ(lk_local_data_track_info(local_track.get(), &local_info), LK_STATUS_OK);
	EXPECT_FALSE(local_info.is_published);
	lk_data_track_snapshot_info_init(&remote_info);
	ASSERT_EQ(lk_remote_data_track_snapshot_info(remote_track_snapshot, &remote_info),
	          LK_STATUS_OK);
	EXPECT_TRUE(remote_info.is_published);
	received.reset();
	reader.reset();
	EXPECT_EQ(lk_local_data_track_destroy(local_track.release()), LK_DATA_TRACK_ERROR_NONE);

	EXPECT_EQ(lk_room_disconnect(sender.get()), LK_STATUS_OK);
	EXPECT_EQ(lk_room_disconnect(receiver.get()), LK_STATUS_OK);
	sender.reset();
	receiver.reset();
	EXPECT_EQ(lk_shutdown(), LK_STATUS_OK);
}

TEST(LiveKitServerTest, CApiReportsParticipantProfileChanges) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the C API "
		                "participant event integration test";
	}

	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();
	auto room_deleter = [](lk_room_t* room) { lk_room_destroy(room); };
	lk_room_t* receiver_handle = nullptr;
	lk_room_t* sender_handle = nullptr;
	ASSERT_EQ(lk_room_create(&receiver_handle), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_create(&sender_handle), LK_STATUS_OK) << lk_last_error();
	std::unique_ptr<lk_room_t, decltype(room_deleter)> receiver(receiver_handle, room_deleter);
	std::unique_ptr<lk_room_t, decltype(room_deleter)> sender(sender_handle, room_deleter);

	CApiParticipantEvents events;
	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	callbacks.user_data = &events;
	callbacks.on_participant_connected = OnCApiParticipantConnected;
	callbacks.on_participant_metadata_changed = OnCApiParticipantMetadataChanged;
	callbacks.on_participant_name_changed = OnCApiParticipantNameChanged;
	callbacks.on_participant_attributes_changed = OnCApiParticipantAttributesChanged;
	ASSERT_EQ(lk_room_set_callbacks(receiver.get(), &callbacks), LK_STATUS_OK) << lk_last_error();

	ASSERT_EQ(lk_room_connect(receiver.get(), url, receiver_token), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_connect(sender.get(), url, sender_token), LK_STATUS_OK) << lk_last_error();
	const auto identity_size = lk_local_participant_identity(sender.get(), nullptr, 0);
	ASSERT_GT(identity_size, 1u);
	std::vector<char> sender_identity(identity_size);
	ASSERT_EQ(
	    lk_local_participant_identity(sender.get(), sender_identity.data(), sender_identity.size()),
	    sender_identity.size());
	ASSERT_TRUE(WaitUntil([&] { return events.connected(sender_identity.data()); },
	                      std::chrono::seconds(10)));

	ASSERT_EQ(lk_local_participant_set_metadata(sender.get(), "c-api-metadata"), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.metadata_changed(sender_identity.data(), "", "c-api-metadata"); },
	    std::chrono::seconds(10)));
	ASSERT_EQ(lk_local_participant_set_name(sender.get(), "c-api-sender"), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(
	    WaitUntil([&] { return events.name_changed(sender_identity.data(), "c-api-sender"); },
	              std::chrono::seconds(10)));
	const std::array<lk_attribute_t, 2> initial_attributes{
	    {{"language", "zh-CN"}, {"role", "publisher"}}};
	ASSERT_EQ(lk_local_participant_set_attributes(sender.get(), initial_attributes.data(),
	                                              initial_attributes.size()),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return events.attributes_changed(sender_identity.data(),
		                                     {{"language", "zh-CN"}, {"role", "publisher"}}, 1);
	    },
	    std::chrono::seconds(10)));

	const lk_attribute_t removed_attribute{"role", ""};
	ASSERT_EQ(lk_local_participant_set_attributes(sender.get(), &removed_attribute, 1),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.attributes_changed(sender_identity.data(), {{"role", ""}}, 2); },
	    std::chrono::seconds(10)));

	EXPECT_EQ(lk_room_disconnect(sender.get()), LK_STATUS_OK);
	EXPECT_EQ(lk_room_disconnect(receiver.get()), LK_STATUS_OK);
	sender.reset();
	receiver.reset();
	EXPECT_EQ(lk_shutdown(), LK_STATUS_OK);
}

TEST(LiveKitServerTest, CApiPreservesDataStreamMetadata) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the C API "
		                "DataStream metadata integration test";
	}

	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();
	auto room_deleter = [](lk_room_t* room) { lk_room_destroy(room); };
	auto text_writer_deleter = [](lk_text_stream_writer_t* writer) {
		lk_text_stream_writer_destroy(writer);
	};
	auto byte_writer_deleter = [](lk_byte_stream_writer_t* writer) {
		lk_byte_stream_writer_destroy(writer);
	};
	lk_room_t* receiver_handle = nullptr;
	lk_room_t* sender_handle = nullptr;
	ASSERT_EQ(lk_room_create(&receiver_handle), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_create(&sender_handle), LK_STATUS_OK) << lk_last_error();
	std::unique_ptr<lk_room_t, decltype(room_deleter)> receiver(receiver_handle, room_deleter);
	std::unique_ptr<lk_room_t, decltype(room_deleter)> sender(sender_handle, room_deleter);

	CApiDataStreamEvents events;
	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	callbacks.user_data = &events;
	callbacks.on_text_received = OnCApiTextReceived;
	callbacks.on_byte_received = OnCApiBytesReceived;
	ASSERT_EQ(lk_room_set_callbacks(receiver.get(), &callbacks), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_register_text_stream_handler(receiver.get(), "c-api-stream-text",
	                                               OnCApiTextStream, &events),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_register_byte_stream_handler(receiver.get(), "c-api-stream-bytes",
	                                               OnCApiByteStream, &events),
	          LK_STATUS_OK)
	    << lk_last_error();

	ASSERT_EQ(lk_room_connect(receiver.get(), url, receiver_token), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_connect(sender.get(), url, sender_token), LK_STATUS_OK) << lk_last_error();
	ASSERT_TRUE(WaitUntil(
	    [&] { return lk_room_is_connected(receiver.get()) && lk_room_is_connected(sender.get()); },
	    std::chrono::seconds(10)));
	auto read_identity = [](lk_room_t* room) {
		std::vector<char> value(lk_local_participant_identity(room, nullptr, 0));
		if (!value.empty()) {
			lk_local_participant_identity(room, value.data(), value.size());
		}
		return value.empty() ? std::string{} : std::string(value.data());
	};
	const auto receiver_identity = read_identity(receiver.get());
	const auto sender_identity = read_identity(sender.get());
	ASSERT_FALSE(receiver_identity.empty());
	ASSERT_FALSE(sender_identity.empty());
	const char* destinations[] = {receiver_identity.c_str()};

	const std::array<lk_attribute_t, 2> text_attributes{
	    {{"language", "zh-CN"}, {"purpose", "parity"}}};
	const char* attached_stream_ids[] = {"ST_attachment"};
	lk_text_send_options_t text_options;
	lk_text_send_options_init(&text_options);
	text_options.topic = "c-api-text-metadata";
	text_options.destination_identities = destinations;
	text_options.destination_identity_count = std::size(destinations);
	text_options.attributes = text_attributes.data();
	text_options.attribute_count = text_attributes.size();
	text_options.reply_to_stream_id = "ST_reply";
	text_options.attached_stream_ids = attached_stream_ids;
	text_options.attached_stream_id_count = std::size(attached_stream_ids);
	ASSERT_EQ(lk_room_send_text(sender.get(), "C API metadata", &text_options), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.LegacyTextMatches(sender_identity); },
	                      std::chrono::seconds(10)));

	const lk_attribute_t byte_attribute{"purpose", "parity"};
	const std::array<uint8_t, 4> bytes{4, 3, 2, 1};
	lk_byte_send_options_t byte_options;
	lk_byte_send_options_init(&byte_options);
	byte_options.topic = "c-api-byte-metadata";
	byte_options.mime_type = "application/x-livekit-metadata";
	byte_options.name = "metadata.bin";
	byte_options.destination_identities = destinations;
	byte_options.destination_identity_count = std::size(destinations);
	byte_options.attributes = &byte_attribute;
	byte_options.attribute_count = 1;
	ASSERT_EQ(lk_room_send_bytes(sender.get(), bytes.data(), bytes.size(), &byte_options),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.LegacyBytesMatch(sender_identity); },
	                      std::chrono::seconds(10)));

	const std::array<lk_attribute_t, 2> stream_text_attributes{
	    {{"language", "zh-CN"}, {"purpose", "stream"}}};
	const char* stream_attachments[] = {"ST_stream_attachment"};
	lk_stream_text_options_t stream_text_options;
	lk_stream_text_options_init(&stream_text_options);
	stream_text_options.topic = "c-api-stream-text";
	stream_text_options.destination_identities = destinations;
	stream_text_options.destination_identity_count = std::size(destinations);
	stream_text_options.attributes = stream_text_attributes.data();
	stream_text_options.attribute_count = stream_text_attributes.size();
	stream_text_options.reply_to_stream_id = "ST_stream_reply";
	stream_text_options.attached_stream_ids = stream_attachments;
	stream_text_options.attached_stream_id_count = std::size(stream_attachments);
	lk_text_stream_writer_t* text_writer_handle = nullptr;
	ASSERT_EQ(lk_room_stream_text(sender.get(), &stream_text_options, &text_writer_handle),
	          LK_STATUS_OK)
	    << lk_last_error();
	std::unique_ptr<lk_text_stream_writer_t, decltype(text_writer_deleter)> text_writer(
	    text_writer_handle, text_writer_deleter);
	ASSERT_EQ(lk_text_stream_writer_write(text_writer.get(), "stream metadata", 15), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_text_stream_writer_close(text_writer.get()), LK_STATUS_OK) << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.TextStreamMatches(sender_identity); },
	                      std::chrono::seconds(10)));

	const lk_attribute_t stream_byte_attribute{"purpose", "stream"};
	lk_stream_bytes_options_t stream_byte_options;
	lk_stream_bytes_options_init(&stream_byte_options);
	stream_byte_options.topic = "c-api-stream-bytes";
	stream_byte_options.mime_type = "application/x-livekit-stream";
	stream_byte_options.name = "stream.bin";
	stream_byte_options.destination_identities = destinations;
	stream_byte_options.destination_identity_count = std::size(destinations);
	stream_byte_options.attributes = &stream_byte_attribute;
	stream_byte_options.attribute_count = 1;
	lk_byte_stream_writer_t* byte_writer_handle = nullptr;
	ASSERT_EQ(lk_room_stream_bytes(sender.get(), &stream_byte_options, &byte_writer_handle),
	          LK_STATUS_OK)
	    << lk_last_error();
	std::unique_ptr<lk_byte_stream_writer_t, decltype(byte_writer_deleter)> byte_writer(
	    byte_writer_handle, byte_writer_deleter);
	const std::array<uint8_t, 4> stream_bytes{1, 3, 5, 7};
	ASSERT_EQ(
	    lk_byte_stream_writer_write(byte_writer.get(), stream_bytes.data(), stream_bytes.size()),
	    LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_byte_stream_writer_close(byte_writer.get()), LK_STATUS_OK) << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.ByteStreamMatches(sender_identity); },
	                      std::chrono::seconds(10)));

	EXPECT_EQ(lk_room_unregister_text_stream_handler(receiver.get(), "c-api-stream-text"),
	          LK_STATUS_OK);
	EXPECT_EQ(lk_room_unregister_byte_stream_handler(receiver.get(), "c-api-stream-bytes"),
	          LK_STATUS_OK);
	text_writer.reset();
	byte_writer.reset();
	EXPECT_EQ(lk_room_disconnect(sender.get()), LK_STATUS_OK);
	EXPECT_EQ(lk_room_disconnect(receiver.get()), LK_STATUS_OK);
	sender.reset();
	receiver.reset();
	EXPECT_EQ(lk_shutdown(), LK_STATUS_OK);
}

TEST(LiveKitServerTest, CApiEncryptsAudioAndDataAndControlsKeys) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the C API "
		                "E2EE integration test";
	}

	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();
	auto room_deleter = [](lk_room_t* room) { lk_room_destroy(room); };
	auto track_deleter = [](lk_local_track_t* track) { (void)lk_local_track_destroy(track); };
	auto source_deleter = [](lk_audio_source_t* source) { (void)lk_audio_source_destroy(source); };
	auto cryptor_deleter = [](lk_frame_cryptor_list_t* cryptors) {
		lk_frame_cryptor_list_destroy(cryptors);
	};

	lk_room_t* receiver_handle = nullptr;
	lk_room_t* sender_handle = nullptr;
	ASSERT_EQ(lk_room_create(&receiver_handle), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_create(&sender_handle), LK_STATUS_OK) << lk_last_error();
	std::unique_ptr<lk_room_t, decltype(room_deleter)> receiver(receiver_handle, room_deleter);
	std::unique_ptr<lk_room_t, decltype(room_deleter)> sender(sender_handle, room_deleter);

	CApiE2eeEvents events;
	lk_room_callbacks_t receiver_callbacks;
	lk_room_callbacks_init(&receiver_callbacks);
	receiver_callbacks.user_data = &events;
	receiver_callbacks.on_audio_frame = OnCApiE2eeAudioFrame;
	receiver_callbacks.on_data_received = OnCApiE2eeData;
	receiver_callbacks.on_encryption_state_changed = OnCApiEncryptionState;
	ASSERT_EQ(lk_room_set_callbacks(receiver.get(), &receiver_callbacks), LK_STATUS_OK);
	lk_room_callbacks_t sender_callbacks;
	lk_room_callbacks_init(&sender_callbacks);
	sender_callbacks.user_data = &events;
	sender_callbacks.on_encryption_state_changed = OnCApiEncryptionState;
	ASSERT_EQ(lk_room_set_callbacks(sender.get(), &sender_callbacks), LK_STATUS_OK);

	const auto shared_key = IntegrationE2eeKey();
	lk_e2ee_options_t e2ee;
	lk_e2ee_options_init(&e2ee);
	e2ee.shared_key = shared_key.data();
	e2ee.shared_key_size = shared_key.size();
	ASSERT_EQ(lk_room_connect_e2ee(receiver.get(), url, receiver_token, &e2ee), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_connect_e2ee(sender.get(), url, sender_token, &e2ee), LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(lk_room_e2ee_is_configured(receiver.get()));
	ASSERT_TRUE(lk_room_e2ee_is_enabled(receiver.get()));
	ASSERT_EQ(lk_room_e2ee_set_enabled(sender.get(), 0), LK_STATUS_OK) << lk_last_error();
	ASSERT_FALSE(lk_room_e2ee_is_enabled(sender.get()));
	ASSERT_EQ(lk_room_e2ee_set_enabled(sender.get(), 1), LK_STATUS_OK) << lk_last_error();

	std::vector<uint8_t> exported(lk_room_e2ee_export_shared_key(sender.get(), 0, nullptr, 0));
	ASSERT_EQ(exported.size(), shared_key.size()) << lk_last_error();
	EXPECT_EQ(lk_room_e2ee_export_shared_key(sender.get(), 0, exported.data(), exported.size()),
	          exported.size());
	EXPECT_EQ(exported, shared_key);

	constexpr std::array<uint8_t, 10> data{'c', '-', 'e', '2', 'e', 'e', '-', 'd', 'a', 't'};
	lk_data_publish_options_t data_options;
	lk_data_publish_options_init(&data_options);
	data_options.topic = "c-api-e2ee";
	ASSERT_EQ(lk_room_publish_data(sender.get(), data.data(), data.size(), &data_options),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.data_received.load(); }, std::chrono::seconds(10)));
	events.data_received.store(false);
	ASSERT_EQ(lk_room_e2ee_ratchet_shared_key(sender.get(), 0), LK_STATUS_OK) << lk_last_error();
	ASSERT_EQ(lk_room_publish_data(sender.get(), data.data(), data.size(), &data_options),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.data_received.load(); }, std::chrono::seconds(10)));

	const std::array<uint8_t, 32> alternate_key{
	    0x8d, 0x41, 0x02, 0x77, 0xc5, 0x19, 0xee, 0x64, 0x37, 0xa8, 0xf2,
	    0x90, 0x1c, 0xb3, 0x56, 0x4a, 0x71, 0x0f, 0xd8, 0x22, 0x9b, 0x6c,
	    0x45, 0xe1, 0xaa, 0x38, 0x73, 0x0d, 0x5f, 0xc4, 0x16, 0x99,
	};
	for (auto* room : {sender.get(), receiver.get()}) {
		ASSERT_EQ(lk_room_e2ee_set_shared_key(room, alternate_key.data(), alternate_key.size(), 1),
		          LK_STATUS_OK)
		    << lk_last_error();
		ASSERT_EQ(lk_room_e2ee_set_data_key_index(room, 1), LK_STATUS_OK) << lk_last_error();
		EXPECT_EQ(lk_room_e2ee_data_key_index(room), 1u);
	}
	events.data_received.store(false);
	ASSERT_EQ(lk_room_publish_data(sender.get(), data.data(), data.size(), &data_options),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_TRUE(WaitUntil([&] { return events.data_received.load(); }, std::chrono::seconds(10)));
	for (auto* room : {sender.get(), receiver.get()}) {
		ASSERT_EQ(lk_room_e2ee_set_data_key_index(room, 0), LK_STATUS_OK) << lk_last_error();
	}

	ASSERT_EQ(lk_room_e2ee_set_participant_key(sender.get(), "test-participant",
	                                           alternate_key.data(), alternate_key.size(), 2),
	          LK_STATUS_OK)
	    << lk_last_error();
	std::vector<uint8_t> participant_key(
	    lk_room_e2ee_export_participant_key(sender.get(), "test-participant", 2, nullptr, 0));
	ASSERT_EQ(participant_key.size(), alternate_key.size());
	EXPECT_EQ(lk_room_e2ee_export_participant_key(sender.get(), "test-participant", 2,
	                                              participant_key.data(), participant_key.size()),
	          participant_key.size());
	EXPECT_TRUE(std::equal(participant_key.begin(), participant_key.end(), alternate_key.begin()));
	ASSERT_EQ(lk_room_e2ee_ratchet_participant_key(sender.get(), "test-participant", 2),
	          LK_STATUS_OK);
	ASSERT_EQ(lk_room_e2ee_remove_participant_key(sender.get(), "test-participant", 2),
	          LK_STATUS_OK);

	lk_audio_source_options_t source_options;
	lk_audio_source_options_init(&source_options);
	lk_audio_source_t* source_handle = nullptr;
	ASSERT_EQ(lk_audio_source_create(&source_options, &source_handle), LK_STATUS_OK)
	    << lk_last_error();
	std::unique_ptr<lk_audio_source_t, decltype(source_deleter)> source(source_handle,
	                                                                    source_deleter);
	lk_local_track_t* track_handle = nullptr;
	ASSERT_EQ(
	    lk_room_create_audio_track(sender.get(), "c-api-e2ee-audio", source.get(), &track_handle),
	    LK_STATUS_OK)
	    << lk_last_error();
	std::unique_ptr<lk_local_track_t, decltype(track_deleter)> track(track_handle, track_deleter);
	lk_track_publish_options_t publish_options;
	lk_track_publish_options_init(&publish_options);
	publish_options.source = LK_TRACK_SOURCE_MICROPHONE;
	ASSERT_EQ(lk_local_track_publish(sender.get(), track.get(), &publish_options), LK_STATUS_OK)
	    << lk_last_error();

	std::vector<int16_t> samples(480, 1700);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return lk_audio_source_capture_frame(source.get(), samples.data(), 480) ==
		               LK_STATUS_OK &&
		           events.audio_frames.load() >= 3 && events.sender_cryptor_ok.load() &&
		           events.receiver_cryptor_ok.load();
	    },
	    std::chrono::seconds(10)));

	lk_frame_cryptor_list_t* sender_cryptors_handle = nullptr;
	lk_frame_cryptor_list_t* receiver_cryptors_handle = nullptr;
	ASSERT_EQ(lk_frame_cryptor_list_create(sender.get(), &sender_cryptors_handle), LK_STATUS_OK);
	ASSERT_EQ(lk_frame_cryptor_list_create(receiver.get(), &receiver_cryptors_handle),
	          LK_STATUS_OK);
	std::unique_ptr<lk_frame_cryptor_list_t, decltype(cryptor_deleter)> sender_cryptors(
	    sender_cryptors_handle, cryptor_deleter);
	std::unique_ptr<lk_frame_cryptor_list_t, decltype(cryptor_deleter)> receiver_cryptors(
	    receiver_cryptors_handle, cryptor_deleter);
	ASSERT_EQ(lk_frame_cryptor_list_count(sender_cryptors.get()), 1u);
	ASSERT_EQ(lk_frame_cryptor_list_count(receiver_cryptors.get()), 1u);
	lk_frame_cryptor_info_t sender_cryptor_info;
	lk_frame_cryptor_info_init(&sender_cryptor_info);
	ASSERT_EQ(lk_frame_cryptor_list_info(sender_cryptors.get(), 0, &sender_cryptor_info),
	          LK_STATUS_OK);
	EXPECT_EQ(sender_cryptor_info.direction, LK_FRAME_CRYPTOR_DIRECTION_SENDER);
	EXPECT_EQ(sender_cryptor_info.state, LK_FRAME_CRYPTOR_STATE_OK);
	const auto track_id_size = lk_frame_cryptor_list_track_id(sender_cryptors.get(), 0, nullptr, 0);
	ASSERT_GT(track_id_size, 1u);
	std::vector<char> track_id(track_id_size);
	EXPECT_EQ(
	    lk_frame_cryptor_list_track_id(sender_cryptors.get(), 0, track_id.data(), track_id.size()),
	    track_id.size());
	lk_frame_cryptor_info_t receiver_cryptor_info;
	lk_frame_cryptor_info_init(&receiver_cryptor_info);
	ASSERT_EQ(lk_frame_cryptor_list_info(receiver_cryptors.get(), 0, &receiver_cryptor_info),
	          LK_STATUS_OK);
	EXPECT_EQ(receiver_cryptor_info.direction, LK_FRAME_CRYPTOR_DIRECTION_RECEIVER);
	EXPECT_EQ(receiver_cryptor_info.state, LK_FRAME_CRYPTOR_STATE_OK);

	ASSERT_EQ(lk_room_e2ee_set_frame_cryptor_enabled(sender.get(), track_id.data(),
	                                                 LK_FRAME_CRYPTOR_DIRECTION_SENDER, 0),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_e2ee_set_frame_cryptor_enabled(sender.get(), track_id.data(),
	                                                 LK_FRAME_CRYPTOR_DIRECTION_SENDER, 1),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_e2ee_set_frame_cryptor_key_index(sender.get(), track_id.data(),
	                                                   LK_FRAME_CRYPTOR_DIRECTION_SENDER, 1),
	          LK_STATUS_OK)
	    << lk_last_error();
	ASSERT_EQ(lk_room_e2ee_set_frame_cryptor_key_index(receiver.get(), track_id.data(),
	                                                   LK_FRAME_CRYPTOR_DIRECTION_RECEIVER, 1),
	          LK_STATUS_OK)
	    << lk_last_error();
	const auto frames_before_slot_change = events.audio_frames.load();
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return lk_audio_source_capture_frame(source.get(), samples.data(), 480) ==
		               LK_STATUS_OK &&
		           events.audio_frames.load() >= frames_before_slot_change + 3;
	    },
	    std::chrono::seconds(10)));

	size_t updated_cryptors = 0;
	const auto sender_identity_size = lk_local_participant_identity(sender.get(), nullptr, 0);
	ASSERT_GT(sender_identity_size, 1u);
	std::vector<char> sender_identity(sender_identity_size);
	ASSERT_EQ(
	    lk_local_participant_identity(sender.get(), sender_identity.data(), sender_identity.size()),
	    sender_identity.size());
	ASSERT_EQ(lk_room_e2ee_set_participant_enabled(receiver.get(), sender_identity.data(), 0,
	                                               &updated_cryptors),
	          LK_STATUS_OK);
	EXPECT_EQ(updated_cryptors, 1u);
	ASSERT_EQ(lk_room_e2ee_set_participant_enabled(receiver.get(), sender_identity.data(), 1,
	                                               &updated_cryptors),
	          LK_STATUS_OK);

	ASSERT_EQ(lk_local_track_unpublish(track.get(), 0), LK_STATUS_OK);
	track.reset();
	source.reset();
	EXPECT_EQ(lk_room_disconnect(sender.get()), LK_STATUS_OK);
	EXPECT_EQ(lk_room_disconnect(receiver.get()), LK_STATUS_OK);
	sender.reset();
	receiver.reset();
	EXPECT_EQ(lk_shutdown(), LK_STATUS_OK);
}

TEST(LiveKitServerTest, EncryptsAudioVideoAndDataEndToEnd) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the E2EE "
		                "integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	const E2eeKey shared_key = IntegrationE2eeKey();
	E2eeOptions e2ee;
	e2ee.encryption_type = EncryptionType::Gcm;
	e2ee.shared_key = shared_key;
	RoomOptions room_options;
	room_options.e2ee = e2ee;

	MediaEvents events;
	auto receiver = CreateRoomUnique();
	auto sender = CreateRoomUnique();
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token, room_options));
	ASSERT_TRUE(sender->Connect(url, sender_token, room_options));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));
	ASSERT_NE(receiver->GetE2EEManager(), nullptr);
	ASSERT_NE(sender->GetE2EEManager(), nullptr);

	const std::vector<uint8_t> initial_data{'e', '2', 'e', 'e', '-', 'd', 'a', 't', 'a'};
	DataPublishOptions data_options;
	data_options.topic = "integration-e2ee-data";
	data_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(initial_data, data_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_data(data_options.topic, initial_data, true); }));

	const E2eeKey alternate_key{
	    0x8d, 0x41, 0x02, 0x77, 0xc5, 0x19, 0xee, 0x64, 0x37, 0xa8, 0xf2,
	    0x90, 0x1c, 0xb3, 0x56, 0x4a, 0x71, 0x0f, 0xd8, 0x22, 0x9b, 0x6c,
	    0x45, 0xe1, 0xaa, 0x38, 0x73, 0x0d, 0x5f, 0xc4, 0x16, 0x99,
	};
	ASSERT_TRUE(sender->GetE2EEManager()->Keys().SetSharedKey(alternate_key, 1).Ok());
	ASSERT_TRUE(receiver->GetE2EEManager()->Keys().SetSharedKey(alternate_key, 1).Ok());
	ASSERT_TRUE(sender->GetE2EEManager()->SetDataKeyIndex(1));
	ASSERT_TRUE(receiver->GetE2EEManager()->SetDataKeyIndex(1));
	const std::vector<uint8_t> alternate_slot_data{'k', 'e', 'y', '-', 's',
	                                               'l', 'o', 't', '-', '1'};
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(alternate_slot_data, data_options));
	ASSERT_TRUE(WaitUntil(
	    [&] { return events.received_data(data_options.topic, alternate_slot_data, true); }));
	ASSERT_TRUE(sender->GetE2EEManager()->SetDataKeyIndex(0));
	ASSERT_TRUE(receiver->GetE2EEManager()->SetDataKeyIndex(0));

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "integration-e2ee-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions audio_options;
	audio_options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(audio_track.get(), audio_options));

	std::vector<int16_t> audio_samples(480, 1700);
	const auto audio_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while ((!events.audio_received() ||
	        !events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Sender,
	                                 FrameCryptorState::Ok) ||
	        !events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
	                                 FrameCryptorState::Ok)) &&
	       std::chrono::steady_clock::now() < audio_deadline) {
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	ASSERT_TRUE(events.audio_received());
	ASSERT_TRUE(events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	ASSERT_TRUE(events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
	                                    FrameCryptorState::Ok));
	auto* remote_sender = receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
	ASSERT_NE(remote_sender, nullptr);
	auto* audio_publication = remote_sender->GetTrackPublication(TrackSource::Microphone);
	ASSERT_NE(audio_publication, nullptr);
	EXPECT_EQ(audio_publication->Encryption(), EncryptionType::Gcm);

	ASSERT_TRUE(sender->GetE2EEManager()->SetFrameCryptorKeyIndex(
	    audio_track->Sid(), FrameCryptorDirection::Sender, 1));
	ASSERT_TRUE(receiver->GetE2EEManager()->SetFrameCryptorKeyIndex(
	    audio_track->Sid(), FrameCryptorDirection::Receiver, 1));
	const auto alternate_slot_audio_frames = events.audio_frame_count() + 5;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480) &&
		           events.audio_frame_count() >= alternate_slot_audio_frames;
	    },
	    std::chrono::seconds(10)));
	ASSERT_TRUE(sender->GetE2EEManager()->SetFrameCryptorKeyIndex(
	    audio_track->Sid(), FrameCryptorDirection::Sender, 0));
	ASSERT_TRUE(receiver->GetE2EEManager()->SetFrameCryptorKeyIndex(
	    audio_track->Sid(), FrameCryptorDirection::Receiver, 0));
	const auto restored_slot_audio_frames = events.audio_frame_count() + 5;
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480) &&
		           events.audio_frame_count() >= restored_slot_audio_frames;
	    },
	    std::chrono::seconds(10)));

	const auto ratchet = sender->GetE2EEManager()->Keys().RatchetSharedKey();
	ASSERT_TRUE(ratchet.Ok()) << (ratchet.error ? ratchet.error->message : "");
	const std::vector<uint8_t> ratcheted_data{'r', 'a', 't', 'c', 'h', 'e', 't', 'e', 'd'};
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishData(ratcheted_data, data_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_data(data_options.topic, ratcheted_data, true); }));

	VideoFrame video_frame;
	video_frame.width = 320;
	video_frame.height = 180;
	video_frame.data.resize(video_frame.width * video_frame.height * 3 / 2, 128);
	std::fill(video_frame.data.begin(),
	          video_frame.data.begin() + video_frame.width * video_frame.height, 72);
	video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                               std::chrono::steady_clock::now().time_since_epoch())
	                               .count();
	auto video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(video_source->CaptureFrame(video_frame));
	auto video_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "integration-e2ee-video", video_source.get());
	ASSERT_NE(video_track, nullptr);
	TrackPublishOptions video_options;
	video_options.source = TrackSource::Camera;
	std::string expected_video_mime_type;
	video_options.video_codec = VideoCodecFromEnvironment(expected_video_mime_type);
	video_options.simulcast = false;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(video_track.get(), video_options));
	const auto video_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while ((!events.video_received() ||
	        !events.encryption_state(video_track->Sid(), FrameCryptorDirection::Sender,
	                                 FrameCryptorState::Ok) ||
	        !events.encryption_state(video_track->Sid(), FrameCryptorDirection::Receiver,
	                                 FrameCryptorState::Ok)) &&
	       std::chrono::steady_clock::now() < video_deadline) {
		video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                               std::chrono::steady_clock::now().time_since_epoch())
		                               .count();
		ASSERT_TRUE(video_source->CaptureFrame(video_frame));
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	std::ostringstream video_cryptor_summary;
	for (const auto& cryptor : receiver->GetE2EEManager()->FrameCryptors()) {
		if (cryptor.track_id == video_track->Sid()) {
			video_cryptor_summary << "[direction=" << static_cast<int>(cryptor.direction)
			                      << ", state=" << static_cast<int>(cryptor.state)
			                      << ", enabled=" << cryptor.enabled
			                      << ", key_index=" << cryptor.key_index << ']';
		}
	}
	const std::string video_sender_stats = video_track->GetRTCStats();
	auto* diagnostic_video_publication = remote_sender->GetTrackPublication(TrackSource::Camera);
	const std::string video_receiver_stats =
	    diagnostic_video_publication != nullptr && diagnostic_video_publication->Track() != nullptr
	        ? diagnostic_video_publication->Track()->GetRTCStats()
	        : "remote video track unavailable";
	ASSERT_TRUE(events.video_received())
	    << "frames=" << events.video_frame_count() << ", sender_e2ee_ok="
	    << events.encryption_state(video_track->Sid(), FrameCryptorDirection::Sender,
	                               FrameCryptorState::Ok)
	    << ", receiver_e2ee_ok="
	    << events.encryption_state(video_track->Sid(), FrameCryptorDirection::Receiver,
	                               FrameCryptorState::Ok)
	    << ", receiver_cryptors=" << video_cryptor_summary.str()
	    << ", sender_stats=" << video_sender_stats << ", receiver_stats=" << video_receiver_stats;
	ASSERT_TRUE(events.encryption_state(video_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	ASSERT_TRUE(events.encryption_state(video_track->Sid(), FrameCryptorDirection::Receiver,
	                                    FrameCryptorState::Ok));
	auto* video_publication = remote_sender->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(video_publication, nullptr);
	EXPECT_EQ(video_publication->MimeType(), expected_video_mime_type);
	EXPECT_EQ(video_publication->Encryption(), EncryptionType::Gcm);

	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
	video_track.reset();
	video_source.reset();
	audio_track.reset();
	audio_source.reset();
}

TEST(LiveKitServerTest, PreservesE2EEAfterPublisherAndSubscriberReconnect) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the E2EE "
		                "reconnect integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	E2eeOptions e2ee;
	e2ee.encryption_type = EncryptionType::Gcm;
	e2ee.shared_key = IntegrationE2eeKey();
	RoomOptions receiver_options;
	receiver_options.e2ee = e2ee;
	auto reconnect_policy = std::make_shared<RecordingReconnectPolicy>();
	RoomOptions sender_options = receiver_options;
	sender_options.reconnect_policy = reconnect_policy;

	MediaEvents events;
	auto receiver = CreateRoomUnique(receiver_options);
	auto sender = CreateRoomUnique(sender_options);
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token, receiver_options));
	ASSERT_TRUE(sender->Connect(url, sender_token, sender_options));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "integration-e2ee-reconnect-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions publish_options;
	publish_options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(audio_track.get(), publish_options));
	std::vector<int16_t> samples(480, 1900);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.audio_received() &&
		           events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Sender,
		                                   FrameCryptorState::Ok) &&
		           events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
		                                   FrameCryptorState::Ok);
	    },
	    std::chrono::seconds(10)));

	auto publish_encrypted_data = [&](std::string topic, const std::vector<uint8_t>& payload) {
		DataPublishOptions options;
		options.topic = std::move(topic);
		options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
		return sender->GetLocalParticipant()->PublishData(payload, options) &&
		       WaitUntil([&] { return events.received_data(options.topic, payload, true); });
	};

	const auto frames_before_publisher_reconnect = events.audio_frame_count();
	auto* concrete_sender = dynamic_cast<Room*>(sender.get());
	ASSERT_NE(concrete_sender, nullptr);
	ASSERT_TRUE(concrete_sender->SimulateMediaFailureForTesting());
	ASSERT_TRUE(WaitUntil([&] { return events.reconnecting(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(WaitUntil([&] { return events.reconnected() && sender->IsConnected(); },
	                      std::chrono::seconds(30)));
	EXPECT_EQ(reconnect_policy->last_reason(), ReconnectReason::MediaFailure);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.local_tracks_published() >= 2 &&
		           events.audio_frame_count() >= frames_before_publisher_reconnect + 3 &&
		           HasFrameCryptorState(sender->GetE2EEManager(), TrackKind::Audio,
		                                FrameCryptorDirection::Sender, FrameCryptorState::Ok) &&
		           HasFrameCryptorState(receiver->GetE2EEManager(), TrackKind::Audio,
		                                FrameCryptorDirection::Receiver, FrameCryptorState::Ok);
	    },
	    std::chrono::seconds(10)));
	ASSERT_TRUE(publish_encrypted_data("e2ee-publisher-reconnected", {1, 4, 1, 5}));

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
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.audio_frame_count() >= frames_before_receiver_reconnect + 3 &&
		           HasFrameCryptorState(receiver->GetE2EEManager(), TrackKind::Audio,
		                                FrameCryptorDirection::Receiver, FrameCryptorState::Ok);
	    },
	    std::chrono::seconds(10)));
	ASSERT_TRUE(publish_encrypted_data("e2ee-subscriber-reconnected", {2, 7, 1, 8}));

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(audio_track.get()));
	audio_track.reset();
	audio_source.reset();
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, ReportsAndRecoversFromE2EEKeyErrors) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the E2EE key "
		                "error integration test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	const E2eeKey shared_key = IntegrationE2eeKey();
	E2eeOptions sender_e2ee;
	sender_e2ee.encryption_type = EncryptionType::Gcm;
	sender_e2ee.shared_key = shared_key;
	RoomOptions sender_options;
	sender_options.e2ee = sender_e2ee;
	E2eeOptions receiver_e2ee;
	receiver_e2ee.encryption_type = EncryptionType::Gcm;
	receiver_e2ee.key_provider.ratchet_window_size = 0;
	receiver_e2ee.key_provider.failure_tolerance = 0;
	RoomOptions receiver_options;
	receiver_options.e2ee = receiver_e2ee;

	MediaEvents events;
	auto receiver = CreateRoomUnique(receiver_options);
	auto sender = CreateRoomUnique(sender_options);
	receiver->AddEventListener(&events);
	sender->AddEventListener(&events);
	ASSERT_TRUE(receiver->Connect(url, receiver_token, receiver_options));
	ASSERT_TRUE(sender->Connect(url, sender_token, sender_options));
	ASSERT_TRUE(WaitUntil([&] { return receiver->IsConnected() && sender->IsConnected(); },
	                      std::chrono::seconds(10)));
	ASSERT_NE(receiver->GetE2EEManager(), nullptr);

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "integration-e2ee-key-error-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions publish_options;
	publish_options.source = TrackSource::Microphone;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(audio_track.get(), publish_options));
	std::vector<int16_t> samples(480, 2100);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
		                                   FrameCryptorState::MissingKey);
	    },
	    std::chrono::seconds(10)));

	const E2eeKey wrong_key(shared_key.size(), 0xa5);
	ASSERT_TRUE(receiver->GetE2EEManager()->Keys().SetSharedKey(wrong_key).Ok());
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
		                                   FrameCryptorState::DecryptionFailed);
	    },
	    std::chrono::seconds(10)));

	const auto frames_before_recovery = events.audio_frame_count();
	ASSERT_TRUE(receiver->GetE2EEManager()->Keys().SetSharedKey(shared_key).Ok());
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return audio_source->CaptureFrame(samples.data(), 48000, 1, 480) &&
		           events.audio_frame_count() >= frames_before_recovery + 5 &&
		           events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Receiver,
		                                   FrameCryptorState::Ok);
	    },
	    std::chrono::seconds(10)));

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(audio_track.get()));
	audio_track.reset();
	audio_source.reset();
	receiver->RemoveEventListener();
	sender->RemoveEventListener();
	EXPECT_TRUE(sender->Disconnect());
	EXPECT_TRUE(receiver->Disconnect());
}

TEST(LiveKitServerTest, InteroperatesWithOfficialJsE2EEPeer) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN");
	const char* peer_identity = std::getenv("LIVEKIT_JS_PEER_IDENTITY");
	if (url == nullptr || token == nullptr || peer_identity == nullptr || *url == '\0' ||
	    *token == '\0' || *peer_identity == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_JS_PEER_IDENTITY to run the "
		                "official JS E2EE interoperability test";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	constexpr std::string_view shared_passphrase = "livekit-cpp-official-js-e2ee";
	const E2eeKey shared_key(shared_passphrase.begin(), shared_passphrase.end());
	E2eeOptions e2ee;
	e2ee.encryption_type = EncryptionType::Gcm;
	e2ee.shared_key = shared_key;
	RoomOptions room_options;
	room_options.e2ee = e2ee;

	MediaEvents events;
	auto room = CreateRoomUnique();
	room->AddEventListener(&events);
	ASSERT_TRUE(room->Connect(url, token, room_options));
	ASSERT_TRUE(WaitUntil([&] { return room->IsConnected(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(
	    WaitUntil([&] { return room->GetRemoteParticipantByIdentity(peer_identity) != nullptr; },
	              std::chrono::seconds(15)));

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = room->GetLocalParticipant()->CreateLocalAudioTrackUnique("cpp-e2ee-js-audio",
	                                                                            audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions audio_options;
	audio_options.source = TrackSource::Microphone;
	ASSERT_TRUE(room->GetLocalParticipant()->PublishTrack(audio_track.get(), audio_options));

	VideoFrame video_frame;
	video_frame.width = 320;
	video_frame.height = 180;
	video_frame.data.resize(video_frame.width * video_frame.height * 3 / 2, 128);
	std::fill(video_frame.data.begin(),
	          video_frame.data.begin() + video_frame.width * video_frame.height, 72);
	video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                               std::chrono::steady_clock::now().time_since_epoch())
	                               .count();
	auto video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(video_source->CaptureFrame(video_frame));
	auto video_track = room->GetLocalParticipant()->CreateLocalVideoTrackUnique("cpp-e2ee-js-video",
	                                                                            video_source.get());
	ASSERT_NE(video_track, nullptr);
	TrackPublishOptions video_options;
	video_options.source = TrackSource::Camera;
	std::string expected_video_mime_type;
	video_options.video_codec = VideoCodecFromEnvironment(expected_video_mime_type);
	video_options.simulcast = false;
	ASSERT_TRUE(room->GetLocalParticipant()->PublishTrack(video_track.get(), video_options));

	const std::vector<uint8_t> outbound_data{'c', 'p', 'p', '-', 'e', '2', 'e', 'e'};
	const std::vector<uint8_t> expected_ack{'j', 's', '-', 'e', '2', 'e', 'e', '-', 'o', 'k'};
	DataPublishOptions data_options;
	data_options.topic = "cpp-e2ee-interop";
	data_options.destination_identities = {peer_identity};
	std::vector<int16_t> audio_samples(480);
	std::size_t audio_sample_offset = 0;
	auto next_data_publish = std::chrono::steady_clock::time_point::min();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
	while (!events.received_data("js-e2ee-interop", expected_ack, true) &&
	       std::chrono::steady_clock::now() < deadline) {
		for (std::size_t index = 0; index < audio_samples.size(); ++index) {
			audio_samples[index] = ((audio_sample_offset + index) % 48) < 24 ? 5000 : -5000;
		}
		audio_sample_offset += audio_samples.size();
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                               std::chrono::steady_clock::now().time_since_epoch())
		                               .count();
		ASSERT_TRUE(video_source->CaptureFrame(video_frame));
		if (std::chrono::steady_clock::now() >= next_data_publish) {
			ASSERT_TRUE(room->GetLocalParticipant()->PublishData(outbound_data, data_options));
			next_data_publish = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	EXPECT_TRUE(events.received_data("js-e2ee-interop", expected_ack, true));
	EXPECT_TRUE(events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	EXPECT_TRUE(events.encryption_state(video_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
	video_track.reset();
	video_source.reset();
	audio_track.reset();
	audio_source.reset();
}

TEST(LiveKitServerTest, InteroperatesWithOfficialCppE2EEPeer) {
	const char* url = std::getenv("LIVEKIT_URL");
	const char* token = std::getenv("LIVEKIT_TOKEN");
	const char* peer_identity = std::getenv("LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY");
	if (url == nullptr || token == nullptr || peer_identity == nullptr || *url == '\0' ||
	    *token == '\0' || *peer_identity == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and "
		                "LIVEKIT_OFFICIAL_CPP_PEER_IDENTITY to run official C++ E2EE interop";
	}

	ClientRuntime runtime;
	ASSERT_TRUE(runtime.initialized());
	constexpr std::string_view shared_passphrase = "livekit-cpp-official-cpp-e2ee";
	E2eeOptions e2ee;
	e2ee.encryption_type = EncryptionType::Gcm;
	e2ee.shared_key = E2eeKey(shared_passphrase.begin(), shared_passphrase.end());
	RoomOptions room_options;
	room_options.e2ee = e2ee;

	MediaEvents events;
	auto room = CreateRoomUnique(room_options);
	room->AddEventListener(&events);
	ASSERT_TRUE(room->Connect(url, token, room_options));
	ASSERT_TRUE(WaitUntil([&] { return room->IsConnected(); }, std::chrono::seconds(10)));
	ASSERT_TRUE(
	    WaitUntil([&] { return room->GetRemoteParticipantByIdentity(peer_identity) != nullptr; },
	              std::chrono::seconds(15)));

	auto audio_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	auto audio_track = room->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    "cpp-e2ee-official-audio", audio_source.get());
	ASSERT_NE(audio_track, nullptr);
	TrackPublishOptions audio_options;
	audio_options.source = TrackSource::Microphone;
	ASSERT_TRUE(room->GetLocalParticipant()->PublishTrack(audio_track.get(), audio_options));

	VideoFrame video_frame;
	video_frame.width = 320;
	video_frame.height = 180;
	video_frame.data.resize(video_frame.width * video_frame.height * 3 / 2, 128);
	std::fill(video_frame.data.begin(),
	          video_frame.data.begin() + video_frame.width * video_frame.height, 72);
	video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
	                               std::chrono::steady_clock::now().time_since_epoch())
	                               .count();
	auto video_source = CreateVideoSourceUnique();
	ASSERT_TRUE(video_source->CaptureFrame(video_frame));
	auto video_track = room->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    "cpp-e2ee-official-video", video_source.get());
	ASSERT_NE(video_track, nullptr);
	TrackPublishOptions video_options;
	video_options.source = TrackSource::Camera;
	std::string expected_video_mime_type;
	video_options.video_codec = VideoCodecFromEnvironment(expected_video_mime_type);
	video_options.simulcast = false;
	ASSERT_TRUE(room->GetLocalParticipant()->PublishTrack(video_track.get(), video_options));

	const std::vector<uint8_t> outbound_data{'c', 'p', 'p', '-', 'o', 'f', 'f', 'i', 'c',
	                                         'i', 'a', 'l', '-', 'e', '2', 'e', 'e'};
	const std::vector<uint8_t> expected_ack{'o', 'f', 'f', 'i', 'c', 'i', 'a', 'l', '-', 'c',
	                                        'p', 'p', '-', 'e', '2', 'e', 'e', '-', 'o', 'k'};
	DataPublishOptions data_options;
	data_options.topic = "cpp-official-e2ee-interop";
	data_options.destination_identities = {peer_identity};
	std::vector<int16_t> audio_samples(480);
	std::size_t audio_sample_offset = 0;
	auto next_data_publish = std::chrono::steady_clock::time_point::min();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!events.received_data("official-cpp-e2ee-interop", expected_ack, true) &&
	       std::chrono::steady_clock::now() < deadline) {
		for (std::size_t index = 0; index < audio_samples.size(); ++index) {
			audio_samples[index] = ((audio_sample_offset + index) % 48) < 24 ? 5000 : -5000;
		}
		audio_sample_offset += audio_samples.size();
		ASSERT_TRUE(audio_source->CaptureFrame(audio_samples.data(), 48000, 1, 480));
		video_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
		                               std::chrono::steady_clock::now().time_since_epoch())
		                               .count();
		ASSERT_TRUE(video_source->CaptureFrame(video_frame));
		if (std::chrono::steady_clock::now() >= next_data_publish) {
			ASSERT_TRUE(room->GetLocalParticipant()->PublishData(outbound_data, data_options));
			next_data_publish = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	EXPECT_TRUE(events.received_data("official-cpp-e2ee-interop", expected_ack, true));
	EXPECT_TRUE(events.encryption_state(audio_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	EXPECT_TRUE(events.encryption_state(video_track->Sid(), FrameCryptorDirection::Sender,
	                                    FrameCryptorState::Ok));
	room->RemoveEventListener();
	EXPECT_TRUE(room->Disconnect());
	video_track.reset();
	video_source.reset();
	audio_track.reset();
	audio_source.reset();
}

TEST(LiveKitServerTest, PublishesAndReceivesHardwareCapturedMedia) {
	const char* hardware_enabled = std::getenv("LIVEKIT_HARDWARE_MEDIA");
	const char* url = std::getenv("LIVEKIT_URL");
	const char* sender_token = std::getenv("LIVEKIT_TOKEN");
	const char* receiver_token = std::getenv("LIVEKIT_TOKEN_2");
	if (hardware_enabled == nullptr || std::string_view(hardware_enabled) != "1") {
		GTEST_SKIP() << "Set LIVEKIT_HARDWARE_MEDIA=1 to run real device capture";
	}
	if (url == nullptr || sender_token == nullptr || receiver_token == nullptr || *url == '\0' ||
	    *sender_token == '\0' || *receiver_token == '\0') {
		GTEST_SKIP() << "Set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_TOKEN_2 to run the hardware "
		                "media integration test";
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

	constexpr std::string_view microphone_name = "hardware-microphone";
	constexpr std::string_view camera_name = "hardware-camera";
	constexpr std::string_view screen_name = "hardware-screen";
	constexpr std::string_view window_name = "hardware-window";
	constexpr std::string_view aec_reference_name = "aec-reference";
	auto microphone_source = CreateMicrophoneAudioSourceUnique();
	ASSERT_NE(microphone_source, nullptr);
	auto camera_source = CreateCameraVideoSourceUnique({{}, 1280, 720, 30});
	ASSERT_NE(camera_source, nullptr);
	ASSERT_TRUE(
	    WaitUntil([&] { return camera_source->Width() == 1280 && camera_source->Height() == 720; },
	              std::chrono::seconds(10)));

	const auto screen_sources = EnumerateScreenCaptureSources();
	const char* window_source_id = std::getenv("LIVEKIT_HARDWARE_WINDOW_SOURCE_ID");
	const bool capture_window = window_source_id != nullptr && *window_source_id != '\0';
	const auto desktop_source =
	    std::find_if(screen_sources.begin(), screen_sources.end(), [&](const auto& source) {
		    return capture_window ? source.id == window_source_id
		                          : source.kind == ScreenCaptureSourceKind::Monitor;
	    });
	ASSERT_NE(desktop_source, screen_sources.end());
	const std::string_view desktop_name = capture_window ? window_name : screen_name;
	auto screen_source = CreateScreenVideoSourceUnique({desktop_source->id, 15, true});
	ASSERT_NE(screen_source, nullptr);
	ASSERT_TRUE(WaitUntil([&] { return screen_source->Width() > 0 && screen_source->Height() > 0; },
	                      std::chrono::seconds(10)));

	auto microphone_track = sender->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    std::string(microphone_name), microphone_source.get());
	auto camera_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    std::string(camera_name), camera_source.get());
	auto screen_track = sender->GetLocalParticipant()->CreateLocalVideoTrackUnique(
	    std::string(desktop_name), screen_source.get());
	ASSERT_NE(microphone_track, nullptr);
	ASSERT_NE(camera_track, nullptr);
	ASSERT_NE(screen_track, nullptr);
	auto aec_reference_source = CreateAudioSourceUnique({}, 48000, 1, 200);
	ASSERT_NE(aec_reference_source, nullptr);
	auto aec_reference_track = receiver->GetLocalParticipant()->CreateLocalAudioTrackUnique(
	    std::string(aec_reference_name), aec_reference_source.get());
	ASSERT_NE(aec_reference_track, nullptr);
	TrackPublishOptions aec_reference_options;
	aec_reference_options.source = TrackSource::Microphone;
	ASSERT_TRUE(receiver->GetLocalParticipant()->PublishTrack(aec_reference_track.get(),
	                                                          aec_reference_options));
	std::vector<int16_t> aec_reference_samples(480, 1200);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    aec_reference_source->CaptureFrame(aec_reference_samples.data(), 48000, 1, 480);
		    return events.media_frames(std::string(aec_reference_name)) >= 20 &&
		           microphone_source->ProcessingStats().render_frames_processed >= 20;
	    },
	    std::chrono::seconds(15)))
	    << "received AEC reference=" << events.media_frames(std::string(aec_reference_name))
	    << ", processed render frames="
	    << microphone_source->ProcessingStats().render_frames_processed;

	TrackPublishOptions microphone_options;
	microphone_options.source = TrackSource::Microphone;
	ASSERT_TRUE(
	    sender->GetLocalParticipant()->PublishTrack(microphone_track.get(), microphone_options));
	ASSERT_TRUE(WaitUntil([&] { return events.media_frames(std::string(microphone_name)) >= 20; },
	                      std::chrono::seconds(15)))
	    << "received microphone=" << events.media_frames(std::string(microphone_name))
	    << ", total audio frames=" << events.audio_frame_count()
	    << ", audio subscriptions=" << events.audio_subscribed_count();
	TrackPublishOptions camera_options;
	camera_options.source = TrackSource::Camera;
	camera_options.simulcast = false;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishTrack(camera_track.get(), camera_options));
	ASSERT_TRUE(WaitUntil([&] { return events.media_frames(std::string(camera_name)) >= 5; },
	                      std::chrono::seconds(15)))
	    << "received camera=" << events.media_frames(std::string(camera_name));
	TrackPublishOptions screen_options;
	screen_options.simulcast = false;
	ASSERT_TRUE(sender->GetLocalParticipant()->PublishScreenShareVideoTrack(screen_track.get(),
	                                                                        screen_options));
	ASSERT_TRUE(WaitUntil([&] { return events.media_frames(std::string(desktop_name)) >= 5; },
	                      std::chrono::seconds(15)))
	    << "received desktop=" << events.media_frames(std::string(desktop_name));
	const auto camera_dimensions = events.video_dimensions(std::string(camera_name));
	const auto screen_dimensions = events.video_dimensions(std::string(desktop_name));
	EXPECT_EQ(camera_dimensions.width, 1280u);
	EXPECT_EQ(camera_dimensions.height, 720u);
	EXPECT_GT(screen_dimensions.width, 0u);
	EXPECT_GT(screen_dimensions.height, 0u);
	const auto microphone_processing = microphone_source->ProcessingStats();
	EXPECT_TRUE(microphone_processing.echo_cancellation_enabled);
	EXPECT_GE(microphone_processing.capture_frames_processed, 20u);
	EXPECT_GE(microphone_processing.render_frames_processed, 20u);
	EXPECT_EQ(microphone_processing.capture_processing_errors, 0u);
	EXPECT_EQ(microphone_processing.render_processing_errors, 0u);
	EXPECT_EQ(microphone_processing.frames_dropped, 0u);

	const auto has_rtp_bytes = [](TrackInterface* track, RTCStatsDirection direction) {
		if (track == nullptr) {
			return false;
		}
		const auto snapshot = track->GetRTCStatsSnapshot();
		return std::any_of(snapshot.streams.begin(), snapshot.streams.end(),
		                   [direction](const auto& stream) {
			                   return stream.direction == direction && stream.bytes > 0;
		                   });
	};
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return has_rtp_bytes(microphone_track.get(), RTCStatsDirection::Send) &&
		           has_rtp_bytes(camera_track.get(), RTCStatsDirection::Send) &&
		           has_rtp_bytes(screen_track.get(), RTCStatsDirection::Send);
	    },
	    std::chrono::seconds(10)));

	auto* remote_sender = receiver->GetRemoteParticipantBySid(sender->GetLocalParticipant()->Sid());
	ASSERT_NE(remote_sender, nullptr);
	auto* remote_microphone = remote_sender->GetTrackPublication(TrackSource::Microphone);
	auto* remote_camera = remote_sender->GetTrackPublication(TrackSource::Camera);
	auto* remote_screen = remote_sender->GetTrackPublication(TrackSource::ScreenShare);
	ASSERT_NE(remote_microphone, nullptr);
	ASSERT_NE(remote_camera, nullptr);
	ASSERT_NE(remote_screen, nullptr);
	ASSERT_TRUE(WaitUntil(
	    [&] {
		    return has_rtp_bytes(remote_microphone->Track(), RTCStatsDirection::Receive) &&
		           has_rtp_bytes(remote_camera->Track(), RTCStatsDirection::Receive) &&
		           has_rtp_bytes(remote_screen->Track(), RTCStatsDirection::Receive);
	    },
	    std::chrono::seconds(10)));

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(screen_track.get()));
	ASSERT_TRUE(WaitUntil(
	    [&] { return remote_sender->GetTrackPublication(TrackSource::ScreenShare) == nullptr; },
	    std::chrono::seconds(10)));
	screen_track.reset();
	screen_source->Stop();
	screen_source.reset();

	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(camera_track.get()));
	EXPECT_TRUE(sender->GetLocalParticipant()->UnpublishTrack(microphone_track.get()));
	EXPECT_TRUE(receiver->GetLocalParticipant()->UnpublishTrack(aec_reference_track.get()));
	aec_reference_track.reset();
	aec_reference_source.reset();
	camera_track.reset();
	microphone_track.reset();
	camera_source->Stop();
	microphone_source->Stop();
	camera_source.reset();
	microphone_source.reset();
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
	text_options.compress = true;
	text_options.destination_identities = {receiver->GetLocalParticipant()->Identity()};
	ASSERT_TRUE(sender->GetLocalParticipant()->SendText("hello from C++", text_options));
	ASSERT_TRUE(
	    WaitUntil([&] { return events.received_text("integration-text", "hello from C++"); }));

	const std::vector<uint8_t> byte_payload{'b', 'y', 't', 'e', 's'};
	ByteSendOptions byte_options;
	byte_options.topic = "integration-bytes";
	byte_options.compress = true;
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
	stream_text_options.compress = true;
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
	stream_byte_options.compress = true;
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
	large_options.compress = true;
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
	file_options.compress = true;
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
