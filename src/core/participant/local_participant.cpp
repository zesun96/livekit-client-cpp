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

#include "../track/audio_source.h"
#include "../track/audio_track.h"
#include "../track/local_audio_track.h"

namespace livekit {
namespace core {
LocalParticipant::LocalParticipant(std::string sid, std::string identity, RtcEngine* engine,
                                   RoomOptions options)
    : engine_(engine), options_(options),
      Participant(sid, identity, "", "", std::map<std::string, std::string>{}) {}

void LocalParticipant::UpdateFromInfo(const livekit::ParticipantInfo info) {}

LocalTrackInterface* LocalParticipant::CreateLocalAudioTreack(std::string label,
                                                              AudioSourceInterface* source) {
	AudioSource* audio_source = dynamic_cast<AudioSource*>(source);
	auto peer_transport_factory_ = engine_->GetSessionPeerTransportFactory();
	if (peer_transport_factory_) {
		auto peer_factory_ = rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(
		    peer_transport_factory_->GetPeerConnectFactory());
		auto rtc_audio_track = peer_factory_->CreateAudioTrack(label, audio_source->Get().get());
		auto audio_track = std::make_unique<AudioTrack>(rtc_audio_track);
		auto local_track = new LocalAudioTrack(std::move(audio_track), source);
		return local_track;
	}
	return nullptr;
}

bool LocalParticipant::PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) {
	return true;
}

} // namespace core
} // namespace livekit