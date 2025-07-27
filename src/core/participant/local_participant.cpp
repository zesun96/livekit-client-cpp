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

#include "livekit_models.pb.h"

namespace livekit {
namespace core {
LocalParticipant::LocalParticipant(std::string sid, std::string identity,
                                   EncryptionType encryption_type, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options), encryption_type_(encryption_type),
      Participant(sid, identity, "", "", std::map<std::string, std::string>{}) {}

void LocalParticipant::UpdateFromInfo(const livekit::ParticipantInfo info) {}

LocalTrackInterface* LocalParticipant::CreateLocalAudioTreack(std::string label,
                                                              AudioSourceInterface* source) {
	AudioSource* audio_source = dynamic_cast<AudioSource*>(source);
	auto peer_transport_factory_ = engine_->GetSessionPeerTransportFactory();
	if (peer_transport_factory_) {
		auto peer_factory_ = rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
		    peer_transport_factory_->GetPeerConnectFactory());
		auto uuid = rtc::CreateRandomUuid();
		auto rtc_audio_track = peer_factory_->CreateAudioTrack(uuid, audio_source->Get().get());
		auto audio_track = std::make_unique<AudioTrack>(rtc_audio_track);
		auto local_track = new LocalAudioTrack(label, std::move(audio_track), source);
		return local_track;
	}
	return nullptr;
}

bool LocalParticipant::PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) {
	auto local_track = dynamic_cast<LocalTrack*>(track);
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

	auto audio_track = dynamic_cast<LocalAudioTrack*>(track);

	try {
		std::cout << "PublishTrack,name" << req.name() << ",kind" << req.type() << std::endl;
		auto option_ti = engine_->AddTrack(req);
		if (!option_ti.has_value()) {
			return false;
		}
		auto& ti = option_ti.value();
		std::string primary_codec_mime;
		auto& codecs = ti.codecs();
		for (auto codec : codecs) {
			primary_codec_mime = codec.mime_type();
			break;
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
		std::cout << "publish track error:" <<  e.what() << std::endl;
		return false;
	}
	return true;
}

} // namespace core
} // namespace livekit
