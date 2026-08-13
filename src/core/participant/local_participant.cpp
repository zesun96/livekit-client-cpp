/**
 *
 * Copyright (c) 2024 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "local_participant.h"

#include "../detail/converted_proto.h"
#include "../track/audio_source.h"
#include "../track/audio_track.h"
#include "../track/local_audio_track.h"
#include "../track/local_video_track.h"
#include "../track/video_source.h"
#include "../track/video_track.h"

#include "livekit/core/room_event_interface.h"
#include "livekit_models.pb.h"
#include "rtc_base/crypto_random.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace livekit {
namespace core {

class OutgoingDataStreamState {
public:
	explicit OutgoingDataStreamState(RtcEngine* engine) : engine_(engine) {}

	bool Send(const livekit::DataPacket& packet) {
		std::lock_guard<std::mutex> guard(mutex_);
		return engine_ != nullptr && engine_->SendDataPacket(packet, true);
	}

	void Invalidate() {
		std::lock_guard<std::mutex> guard(mutex_);
		engine_ = nullptr;
	}

private:
	std::mutex mutex_;
	RtcEngine* engine_;
};

namespace {

constexpr std::size_t kMaximumDataStreamChunkSize = 15'000;

int64_t CurrentTimestampMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

template <typename Options>
void PopulateStreamPacket(livekit::DataPacket& packet, const Options& options) {
	for (const auto& identity : options.destination_identities) {
		packet.add_destination_identities(identity);
	}
}

template <typename Options>
void PopulateStreamHeader(livekit::DataStream_Header& header, const std::string& stream_id,
                          uint64_t total_length, const Options& options) {
	header.set_stream_id(stream_id);
	header.set_timestamp(CurrentTimestampMilliseconds());
	header.set_topic(options.topic);
	header.set_total_length(total_length);
	for (const auto& [key, value] : options.attributes) {
		(*header.mutable_attributes())[key] = value;
	}
}

template <typename Options>
bool SendStreamPayload(RtcEngine* engine, const std::string& stream_id, const uint8_t* data,
                       std::size_t size, const Options& options) {
	if (engine == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize || (data == nullptr && size != 0)) {
		return false;
	}
	uint64_t chunk_index = 0;
	for (std::size_t offset = 0; offset < size; offset += options.chunk_size) {
		const auto count = std::min(options.chunk_size, size - offset);
		livekit::DataPacket chunk_packet;
		PopulateStreamPacket(chunk_packet, options);
		auto* chunk = chunk_packet.mutable_stream_chunk();
		chunk->set_stream_id(stream_id);
		chunk->set_chunk_index(chunk_index++);
		chunk->set_content(data + offset, count);
		if (!engine->SendDataPacket(chunk_packet, true)) {
			return false;
		}
	}

	livekit::DataPacket trailer_packet;
	PopulateStreamPacket(trailer_packet, options);
	trailer_packet.mutable_stream_trailer()->set_stream_id(stream_id);
	return engine->SendDataPacket(trailer_packet, true);
}

template <typename Info>
void PopulateStreamingHeader(livekit::DataStream_Header& header, const Info& info) {
	header.set_stream_id(info.stream_id);
	header.set_timestamp(info.timestamp);
	header.set_topic(info.topic);
	header.set_mime_type(info.mime_type);
	if (info.total_size.has_value()) {
		header.set_total_length(*info.total_size);
	}
	for (const auto& [key, value] : info.attributes) {
		(*header.mutable_attributes())[key] = value;
	}
}

bool SendStreamTrailer(const std::shared_ptr<OutgoingDataStreamState>& state,
                       const std::vector<std::string>& destination_identities,
                       const std::string& stream_id, const std::string& reason) {
	livekit::DataPacket packet;
	for (const auto& identity : destination_identities) {
		packet.add_destination_identities(identity);
	}
	auto* trailer = packet.mutable_stream_trailer();
	trailer->set_stream_id(stream_id);
	trailer->set_reason(reason);
	return state && state->Send(packet);
}

class OutgoingStreamWriterBase {
public:
	OutgoingStreamWriterBase(std::shared_ptr<OutgoingDataStreamState> state, DataStreamInfo info,
	                         std::vector<std::string> destination_identities,
	                         std::size_t chunk_size, int32_t version,
	                         DataStreamProgressHandler progress)
	    : state_(std::move(state)), info_(std::move(info)),
	      destination_identities_(std::move(destination_identities)), chunk_size_(chunk_size),
	      version_(version), progress_(std::move(progress)) {}

	virtual ~OutgoingStreamWriterBase() { CancelInternal("writer destroyed before close"); }

	bool IsClosedInternal() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return closed_;
	}

	bool WriteBytes(const uint8_t* data, std::size_t size, bool preserve_utf8_boundaries) {
		DataStreamProgressHandler progress;
		uint64_t bytes_sent = 0;
		std::optional<uint64_t> total_size;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_ || (data == nullptr && size != 0)) {
				return false;
			}
			for (std::size_t offset = 0; offset < size;) {
				std::size_t count = std::min(chunk_size_, size - offset);
				if (preserve_utf8_boundaries && offset + count < size) {
					while (count > 0 && (data[offset + count] & 0xc0U) == 0x80U) {
						--count;
					}
					if (count == 0) {
						return false;
					}
				}
				livekit::DataPacket packet;
				for (const auto& identity : destination_identities_) {
					packet.add_destination_identities(identity);
				}
				auto* chunk = packet.mutable_stream_chunk();
				chunk->set_stream_id(info_.stream_id);
				chunk->set_chunk_index(next_chunk_++);
				chunk->set_version(version_);
				chunk->set_content(data + offset, count);
				if (!state_ || !state_->Send(packet)) {
					closed_ = true;
					return false;
				}
				offset += count;
				bytes_sent_ += count;
			}
			progress = progress_;
			bytes_sent = bytes_sent_;
			total_size = info_.total_size;
		}
		if (progress) {
			progress(bytes_sent, total_size);
		}
		return true;
	}

	bool CloseInternal() {
		bool complete = true;
		DataStreamProgressHandler progress;
		uint64_t bytes_sent = 0;
		std::optional<uint64_t> total_size;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_) {
				return false;
			}
			closed_ = true;
			complete = !info_.total_size.has_value() || bytes_sent_ == *info_.total_size;
			if (!SendStreamTrailer(state_, destination_identities_, info_.stream_id,
			                       complete ? "" : "incomplete stream")) {
				return false;
			}
			progress = progress_;
			bytes_sent = bytes_sent_;
			total_size = info_.total_size;
		}
		if (complete && progress) {
			progress(bytes_sent, total_size);
		}
		return complete;
	}

	bool CancelInternal(std::string reason) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (closed_) {
			return false;
		}
		closed_ = true;
		if (reason.empty()) {
			reason = "cancelled";
		}
		return SendStreamTrailer(state_, destination_identities_, info_.stream_id, reason);
	}

protected:
	DataStreamInfo info_;

private:
	std::shared_ptr<OutgoingDataStreamState> state_;
	std::vector<std::string> destination_identities_;
	std::size_t chunk_size_;
	int32_t version_;
	DataStreamProgressHandler progress_;
	mutable std::mutex mutex_;
	uint64_t next_chunk_ = 0;
	uint64_t bytes_sent_ = 0;
	bool closed_ = false;
};

class TextStreamWriter final : public TextStreamWriterInterface, private OutgoingStreamWriterBase {
public:
	TextStreamWriter(std::shared_ptr<OutgoingDataStreamState> state, TextStreamInfo info,
	                 StreamTextOptions options)
	    : OutgoingStreamWriterBase(std::move(state), info,
	                               std::move(options.destination_identities), options.chunk_size,
	                               options.version, std::move(options.on_progress)),
	      text_info_(std::move(info)) {}

	TextStreamInfo Info() const override { return text_info_; }
	bool Write(const std::string& text) override {
		return WriteBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size(), true);
	}
	bool Close() override { return CloseInternal(); }
	bool Cancel(std::string reason) override { return CancelInternal(std::move(reason)); }
	bool IsClosed() const override { return IsClosedInternal(); }

private:
	TextStreamInfo text_info_;
};

class ByteStreamWriter final : public ByteStreamWriterInterface, private OutgoingStreamWriterBase {
public:
	ByteStreamWriter(std::shared_ptr<OutgoingDataStreamState> state, ByteStreamInfo info,
	                 StreamBytesOptions options)
	    : OutgoingStreamWriterBase(std::move(state), info,
	                               std::move(options.destination_identities), options.chunk_size, 0,
	                               std::move(options.on_progress)),
	      byte_info_(std::move(info)) {}

	ByteStreamInfo Info() const override { return byte_info_; }
	bool Write(const std::vector<uint8_t>& data) override {
		return WriteBytes(data.data(), data.size(), false);
	}
	bool Close() override { return CloseInternal(); }
	bool Cancel(std::string reason) override { return CancelInternal(std::move(reason)); }
	bool IsClosed() const override { return IsClosedInternal(); }

private:
	ByteStreamInfo byte_info_;
};

} // namespace

LocalParticipant::LocalParticipant(std::string sid, std::string identity,
                                   EncryptionType encryption_type, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options), encryption_type_(encryption_type),
      Participant(std::move(sid), std::move(identity), "", "",
                  std::map<std::string, std::string>{}),
      outgoing_stream_state_(std::make_shared<OutgoingDataStreamState>(engine)) {
	is_local_participant_ = true;
}

LocalParticipant::~LocalParticipant() { outgoing_stream_state_->Invalidate(); }

void LocalParticipant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	const auto owned_publications = TrackPublicationsSnapshot();
	Participant::UpdateFromInfo(info);
	// Participant snapshots can briefly omit a newly published local track while negotiation is
	// settling. Keep client-owned publications (and their LocalTrack pointers) until an explicit
	// unpublish operation or TrackUnpublishedResponse removes them.
	for (const auto& [sid, publication] : owned_publications) {
		if (dynamic_cast<LocalTrackPublication*>(publication.get()) != nullptr) {
			AddTrackPublication(publication);
		}
	}
	std::lock_guard<std::mutex> guard(participant_mutex_);
	is_local_participant_ = true;
}

void LocalParticipant::UpdateFromInfoPreservingTracks(const livekit::ParticipantInfo& info) {
	UpdateInfoFields(info);
	std::lock_guard<std::mutex> guard(participant_mutex_);
	is_local_participant_ = true;
}

LocalTrackInterface* LocalParticipant::CreateLocalAudioTreack(std::string label,
                                                              AudioSourceInterface* source) {
	if (engine_ == nullptr || source == nullptr) {
		return nullptr;
	}
	auto* audio_source = dynamic_cast<AudioSource*>(source);
	if (audio_source == nullptr) {
		return nullptr;
	}
	auto peer_transport_factory_ = engine_->GetSessionPeerTransportFactory();
	if (peer_transport_factory_) {
		auto peer_factory_ = webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
		    peer_transport_factory_->GetPeerConnectFactory());
		if (!peer_factory_) {
			return nullptr;
		}
		auto uuid = webrtc::CreateRandomUuid();
		auto rtc_audio_track = peer_factory_->CreateAudioTrack(uuid, audio_source->Get().get());
		if (!rtc_audio_track) {
			return nullptr;
		}
		auto audio_track = std::make_unique<AudioTrack>(rtc_audio_track);
		auto local_track = new LocalAudioTrack(label, std::move(audio_track), source);
		return local_track;
	}
	return nullptr;
}

LocalTrackInterface* LocalParticipant::CreateLocalVideoTrack(std::string label,
                                                             VideoSourceInterface* source) {
	if (engine_ == nullptr || source == nullptr) {
		return nullptr;
	}
	auto* video_source = dynamic_cast<VideoSource*>(source);
	if (video_source == nullptr) {
		return nullptr;
	}
	auto peer_transport_factory = engine_->GetSessionPeerTransportFactory();
	if (!peer_transport_factory) {
		return nullptr;
	}
	auto peer_factory = webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
	    peer_transport_factory->GetPeerConnectFactory());
	if (!peer_factory) {
		return nullptr;
	}
	auto rtc_video_track =
	    peer_factory->CreateVideoTrack(video_source->Get(), webrtc::CreateRandomUuid());
	if (!rtc_video_track) {
		return nullptr;
	}
	auto video_track = std::make_unique<VideoTrack>(std::move(rtc_video_track));
	return new LocalVideoTrack(std::move(label), std::move(video_track), source);
}

bool LocalParticipant::PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) {
	if (engine_ == nullptr || track == nullptr) {
		return false;
	}
	auto* local_track = dynamic_cast<LocalTrack*>(track);
	if (local_track == nullptr || local_track->media_track() == nullptr ||
	    local_track->media_track()->rtc_track() == nullptr) {
		return false;
	}
	auto kind = local_track->Kind();
	auto cid = local_track->media_track()->rtc_track()->id();
	auto req = livekit::AddTrackRequest();
	req.set_cid(cid);
	req.set_name(local_track->Name());
	req.set_type(to_proto(kind));
	req.set_source(to_proto(option.source));
	req.set_muted(local_track->Muted());
	req.set_disable_dtx(!option.dtx);
	req.set_disable_red(!option.red);
	req.set_encryption(to_proto(encryption_type_));
	req.set_stream(option.stream);
	if (kind == TrackKind::Video) {
		auto* video_track = dynamic_cast<LocalVideoTrack*>(local_track);
		if (video_track == nullptr || video_track->source() == nullptr ||
		    video_track->source()->Width() == 0 || video_track->source()->Height() == 0) {
			return false;
		}
		req.set_width(video_track->source()->Width());
		req.set_height(video_track->source()->Height());
	}

	try {
		std::cout << "PublishTrack,name" << req.name() << ",kind" << req.type() << std::endl;
		auto option_ti = engine_->AddTrack(req);
		if (!option_ti.has_value()) {
			return false;
		}
		auto& ti = option_ti.value();
		std::string primary_codec_mime;
		const auto& codecs = ti.codecs();
		if (!codecs.empty()) {
			primary_codec_mime = codecs.Get(0).mime_type();
		}

		if (!primary_codec_mime.empty() && kind == TrackKind::Video) {
		}

		auto publication = std::make_shared<LocalTrackPublication>(ti, local_track);
		local_track->UpdateInfo(ti);
		std::vector<webrtc::RtpEncodingParameters> send_encodings;
		auto transceiver = engine_->CreateSender(local_track, option, send_encodings);

		if (!transceiver) {
			return false;
		}
		local_track->SetTransceiver(transceiver);

		engine_->PublisherNegotiationNeeded();

		publication->UpdatePublishOptions(option);

		this->AddTrackPublication(publication);

		local_track->SetEnabled(true);
		if (auto* listener = event_listener_.load()) {
			listener->OnLocalTrackPublished(publication.get(), this);
		}

	} catch (const std::exception& e) {
		std::cout << "publish track error:" << e.what() << std::endl;
		return false;
	}
	return true;
}

bool LocalParticipant::UnpublishTrack(LocalTrackInterface* track, bool stop_on_unpublish) {
	if (engine_ == nullptr || track == nullptr) {
		return false;
	}
	auto* local_track = dynamic_cast<LocalTrack*>(track);
	if (local_track == nullptr || local_track->Sid().empty() || !local_track->Transceiver()) {
		return false;
	}
	auto publications = TrackPublicationsSnapshot();
	auto publication = publications.find(local_track->Sid());
	if (publication == publications.end() || publication->second->Track() != local_track ||
	    !engine_->RemoveSender(local_track)) {
		return false;
	}

	local_track->SetTransceiver(nullptr);
	RemoveTrackPublication(local_track->Sid());
	if (stop_on_unpublish) {
		local_track->SetEnabled(false);
	}
	engine_->PublisherNegotiationNeeded();
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalTrackUnpublished(publication->second.get(), this);
	}
	return true;
}

std::size_t LocalParticipant::UnpublishTracks(const std::vector<LocalTrackInterface*>& tracks,
                                              bool stop_on_unpublish) {
	std::size_t unpublished = 0;
	for (auto* track : tracks) {
		if (UnpublishTrack(track, stop_on_unpublish)) {
			++unpublished;
		}
	}
	return unpublished;
}

bool LocalParticipant::RepublishAllTracks() {
	struct PendingTrack {
		LocalTrackInterface* track;
		TrackPublishOptions options;
	};

	std::vector<PendingTrack> tracks;
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrackInterface*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			tracks.push_back({local_track, local_publication->PublishOptions()});
		}
	}

	bool success = true;
	for (const auto& pending : tracks) {
		if (!UnpublishTrack(pending.track, false) ||
		    !PublishTrack(pending.track, pending.options)) {
			success = false;
		}
	}
	return success;
}

void LocalParticipant::DetachTrackTransceiversForReconnect() {
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrack*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			local_track->SetTransceiver(nullptr);
		}
	}
}

bool LocalParticipant::RepublishAllTracksAfterReconnect() {
	struct PendingTrack {
		std::string publication_sid;
		LocalTrack* track;
		TrackPublishOptions options;
	};

	std::vector<PendingTrack> tracks;
	for (const auto& [sid, publication] : TrackPublicationsSnapshot()) {
		auto* local_publication = dynamic_cast<LocalTrackPublication*>(publication.get());
		auto* local_track = local_publication != nullptr
		                        ? dynamic_cast<LocalTrack*>(publication->Track())
		                        : nullptr;
		if (local_track != nullptr) {
			tracks.push_back({sid, local_track, local_publication->PublishOptions()});
		}
	}

	for (const auto& pending : tracks) {
		RemoveTrackPublication(pending.publication_sid);
	}

	bool success = true;
	for (const auto& pending : tracks) {
		if (!PublishTrack(pending.track, pending.options)) {
			success = false;
		}
	}
	return success;
}

void LocalParticipant::SetEventListener(RoomEventInterface* listener) {
	event_listener_.store(listener);
}

bool LocalParticipant::SetMetadata(const std::string& metadata) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(metadata, Name(), {});
}

bool LocalParticipant::SetName(const std::string& name) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(Metadata(), name, {});
}

bool LocalParticipant::SetAttributes(const std::map<std::string, std::string>& attributes) {
	return engine_ != nullptr && engine_->UpdateLocalMetadata(Metadata(), Name(), attributes);
}

bool LocalParticipant::PublishData(const std::vector<uint8_t>& data, DataPublishOptions options) {
	if (engine_ == nullptr) {
		return false;
	}
	livekit::DataPacket packet;
	packet.set_kind(options.reliable ? livekit::DataPacket_Kind_RELIABLE
	                                 : livekit::DataPacket_Kind_LOSSY);
	for (const auto& identity : options.destination_identities) {
		packet.add_destination_identities(identity);
	}
	auto* user = packet.mutable_user();
	user->set_participant_identity(Identity());
	user->set_payload(data.data(), data.size());
	if (!options.topic.empty()) {
		user->set_topic(std::move(options.topic));
	}
	return engine_->SendDataPacket(packet, options.reliable);
}

RpcResult LocalParticipant::PerformRpc(const PerformRpcParams& params) {
	if (engine_ == nullptr) {
		return RpcResult::Failure(RpcError::BuiltIn(RpcErrorCode::SendFailed));
	}
	return engine_->PerformRpc(params);
}

bool LocalParticipant::SetTrackSubscriptionPermissions(
    bool all_participants_allowed,
    const std::vector<ParticipantTrackPermission>& participant_permissions) {
	for (const auto& permission : participant_permissions) {
		if (permission.participant_sid.empty() && permission.participant_identity.empty()) {
			return false;
		}
		if (std::any_of(permission.allowed_track_sids.begin(), permission.allowed_track_sids.end(),
		                [](const std::string& sid) { return sid.empty(); })) {
			return false;
		}
	}
	{
		std::lock_guard<std::mutex> guard(subscription_permissions_mutex_);
		all_participants_allowed_to_subscribe_ = all_participants_allowed;
		participant_track_permissions_ = participant_permissions;
	}
	// Matching the official clients, permissions may be configured before Connect(). They are
	// retained and sent by ResendTrackSubscriptionPermissions() once signaling is available.
	if (engine_ != nullptr) {
		engine_->UpdateSubscriptionPermissions(all_participants_allowed, participant_permissions);
	}
	return true;
}

bool LocalParticipant::ResendTrackSubscriptionPermissions() {
	bool all_participants_allowed = true;
	std::vector<ParticipantTrackPermission> permissions;
	{
		std::lock_guard<std::mutex> guard(subscription_permissions_mutex_);
		all_participants_allowed = all_participants_allowed_to_subscribe_;
		permissions = participant_track_permissions_;
	}
	return engine_ != nullptr &&
	       engine_->UpdateSubscriptionPermissions(all_participants_allowed, permissions);
}

bool LocalParticipant::SendText(const std::string& text, TextSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize) {
		return false;
	}
	const std::string stream_id = webrtc::CreateRandomUuid();
	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, text.size(), options);
	header->set_mime_type("text/plain");
	auto* text_header = header->mutable_text_header();
	text_header->set_operation_type(livekit::DataStream_OperationType_CREATE);
	text_header->set_reply_to_stream_id(options.reply_to_stream_id);
	for (const auto& attached_stream_id : options.attached_stream_ids) {
		text_header->add_attached_stream_ids(attached_stream_id);
	}
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}
	return SendStreamPayload(engine_, stream_id, reinterpret_cast<const uint8_t*>(text.data()),
	                         text.size(), options);
}

bool LocalParticipant::SendBytes(const std::vector<uint8_t>& data, ByteSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize) {
		return false;
	}
	const std::string stream_id = webrtc::CreateRandomUuid();
	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, data.size(), options);
	header->set_mime_type(options.mime_type);
	header->mutable_byte_header()->set_name(options.name);
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}
	return SendStreamPayload(engine_, stream_id, data.data(), data.size(), options);
}

bool LocalParticipant::SendFile(const std::string& path, FileSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize) {
		return false;
	}
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		return false;
	}
	const auto end = input.tellg();
	if (end < 0) {
		return false;
	}
	const auto file_size = static_cast<uint64_t>(end);
	input.seekg(0, std::ios::beg);

	const std::string stream_id = webrtc::CreateRandomUuid();

	livekit::DataPacket header_packet;
	PopulateStreamPacket(header_packet, options);
	auto* header = header_packet.mutable_stream_header();
	PopulateStreamHeader(*header, stream_id, file_size, options);
	header->set_mime_type(options.mime_type);
	header->mutable_byte_header()->set_name(std::filesystem::path(path).filename().string());
	if (!engine_->SendDataPacket(header_packet, true)) {
		return false;
	}

	std::vector<char> buffer(options.chunk_size);
	uint64_t chunk_index = 0;
	while (input) {
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const auto count = input.gcount();
		if (count <= 0) {
			break;
		}
		livekit::DataPacket chunk_packet;
		PopulateStreamPacket(chunk_packet, options);
		auto* chunk = chunk_packet.mutable_stream_chunk();
		chunk->set_stream_id(stream_id);
		chunk->set_chunk_index(chunk_index++);
		chunk->set_content(buffer.data(), static_cast<std::size_t>(count));
		if (!engine_->SendDataPacket(chunk_packet, true)) {
			return false;
		}
	}

	livekit::DataPacket trailer_packet;
	PopulateStreamPacket(trailer_packet, options);
	trailer_packet.mutable_stream_trailer()->set_stream_id(stream_id);
	return engine_->SendDataPacket(trailer_packet, true);
}

std::unique_ptr<TextStreamWriterInterface> LocalParticipant::StreamText(StreamTextOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize ||
	    (options.update && options.stream_id.empty())) {
		return nullptr;
	}
	TextStreamInfo info;
	info.stream_id = options.stream_id.empty() ? webrtc::CreateRandomUuid() : options.stream_id;
	info.mime_type = "text/plain";
	info.topic = options.topic;
	info.attributes = options.attributes;
	info.total_size = options.total_size;
	info.timestamp = CurrentTimestampMilliseconds();
	info.reply_to_stream_id = options.reply_to_stream_id;
	info.attached_stream_ids = options.attached_stream_ids;
	livekit::DataPacket packet;
	PopulateStreamPacket(packet, options);
	auto* header = packet.mutable_stream_header();
	PopulateStreamingHeader(*header, info);
	auto* text_header = header->mutable_text_header();
	text_header->set_operation_type(options.update ? livekit::DataStream_OperationType_UPDATE
	                                               : livekit::DataStream_OperationType_CREATE);
	text_header->set_version(options.version);
	text_header->set_reply_to_stream_id(options.reply_to_stream_id);
	for (const auto& stream_id : options.attached_stream_ids) {
		text_header->add_attached_stream_ids(stream_id);
	}
	if (!outgoing_stream_state_->Send(packet)) {
		return nullptr;
	}
	return std::make_unique<TextStreamWriter>(outgoing_stream_state_, std::move(info),
	                                          std::move(options));
}

std::unique_ptr<ByteStreamWriterInterface>
LocalParticipant::StreamBytes(StreamBytesOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 ||
	    options.chunk_size > kMaximumDataStreamChunkSize) {
		return nullptr;
	}
	ByteStreamInfo info;
	info.stream_id = options.stream_id.empty() ? webrtc::CreateRandomUuid() : options.stream_id;
	info.mime_type = options.mime_type;
	info.topic = options.topic;
	info.attributes = options.attributes;
	info.total_size = options.total_size;
	info.timestamp = CurrentTimestampMilliseconds();
	info.name = options.name;
	livekit::DataPacket packet;
	PopulateStreamPacket(packet, options);
	auto* header = packet.mutable_stream_header();
	PopulateStreamingHeader(*header, info);
	header->mutable_byte_header()->set_name(options.name);
	if (!outgoing_stream_state_->Send(packet)) {
		return nullptr;
	}
	return std::make_unique<ByteStreamWriter>(outgoing_stream_state_, std::move(info),
	                                          std::move(options));
}

} // namespace core
} // namespace livekit
