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

#include "livekit_models.pb.h"
#include "rtc_base/crypto_random.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace livekit {
namespace core {
LocalParticipant::LocalParticipant(std::string sid, std::string identity,
                                   EncryptionType encryption_type, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options), encryption_type_(encryption_type),
      Participant(std::move(sid), std::move(identity), "", "",
                  std::map<std::string, std::string>{}) {
	is_local_participant_ = true;
}

void LocalParticipant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	Participant::UpdateFromInfo(info);
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

		local_track->media_track()->set_enabled(true);

	} catch (const std::exception& e) {
		std::cout << "publish track error:" << e.what() << std::endl;
		return false;
	}
	return true;
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

bool LocalParticipant::SendFile(const std::string& path, FileSendOptions options) {
	if (engine_ == nullptr || options.chunk_size == 0 || options.chunk_size > 15'000) {
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
	auto add_destinations = [&options](livekit::DataPacket& packet) {
		for (const auto& identity : options.destination_identities) {
			packet.add_destination_identities(identity);
		}
	};

	livekit::DataPacket header_packet;
	add_destinations(header_packet);
	auto* header = header_packet.mutable_stream_header();
	header->set_stream_id(stream_id);
	header->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
	                          std::chrono::system_clock::now().time_since_epoch())
	                          .count());
	header->set_topic(options.topic);
	header->set_mime_type(options.mime_type);
	header->set_total_length(file_size);
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
		add_destinations(chunk_packet);
		auto* chunk = chunk_packet.mutable_stream_chunk();
		chunk->set_stream_id(stream_id);
		chunk->set_chunk_index(chunk_index++);
		chunk->set_content(buffer.data(), static_cast<std::size_t>(count));
		if (!engine_->SendDataPacket(chunk_packet, true)) {
			return false;
		}
	}

	livekit::DataPacket trailer_packet;
	add_destinations(trailer_packet);
	trailer_packet.mutable_stream_trailer()->set_stream_id(stream_id);
	return engine_->SendDataPacket(trailer_packet, true);
}

} // namespace core
} // namespace livekit
