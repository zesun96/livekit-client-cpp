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

#include "remote_participant.h"

namespace livekit {
namespace core {
RemoteParticipant::RemoteParticipant(const livekit::ParticipantInfo& info)
    : RemoteParticipant(info, true, {}) {}

RemoteParticipant::RemoteParticipant(const livekit::ParticipantInfo& info, bool auto_subscribe,
                                     PublicationHandlers handlers)
    : Participant("", "", "", "", {}), auto_subscribe_(auto_subscribe),
      handlers_(std::move(handlers)) {
	Participant::UpdateFromInfo(info);
}

void RemoteParticipant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	Participant::UpdateFromInfo(info);
}

std::shared_ptr<TrackPublicationInterface>
RemoteParticipant::CreateTrackPublication(const livekit::TrackInfo& info) {
	return std::make_shared<RemoteTrackPublication>(info, auto_subscribe_, handlers_.subscription,
	                                                handlers_.settings, handlers_.status);
}
} // namespace core
} // namespace livekit
