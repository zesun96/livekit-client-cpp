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

} // namespace

LocalParticipant::LocalParticipant(std::string sid, std::string identity,
                                   EncryptionType encryption_type, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options), encryption_type_(encryption_type),
      Participant(std::move(sid), std::move(identity), "", "",
                  std::map<std::string, std::string>{}) {
	is_local_participant_ = true;
}

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

} // namespace core
} // namespace livekit
