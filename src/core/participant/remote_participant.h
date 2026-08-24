/**
 *
 * Copyright (c) 2025 sunze
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

#pragma once

#ifndef _LKC_CORE_PARTICIPANT_REMOTE_PARTICIPANT_H_
#define _LKC_CORE_PARTICIPANT_REMOTE_PARTICIPANT_H_

#include "../data_track.h"
#include "../track/remote_track_publication.h"
#include "livekit/core/participant/remote_participant_interface.h"
#include "participant.h"

#include <functional>
#include <string>

namespace livekit {
namespace core {

class RemoteParticipant : public Participant, public RemoteParticipantInterface {
public:
	struct PublicationHandlers {
		RemoteTrackPublication::SubscriptionHandler subscription;
		RemoteTrackPublication::SettingsHandler settings;
		RemoteTrackPublication::StatusHandler status;
	};
	using DataTrackSubscriptionHandler = RemoteDataTrack::SubscriptionHandler;

	explicit RemoteParticipant(const livekit::ParticipantInfo& info);
	RemoteParticipant(const livekit::ParticipantInfo& info, bool auto_subscribe,
	                  PublicationHandlers handlers,
	                  DataTrackSubscriptionHandler data_track_subscription = {});
	virtual ~RemoteParticipant() = default;
	virtual void UpdateFromInfo(const livekit::ParticipantInfo& info) override;

protected:
	std::shared_ptr<TrackPublicationInterface>
	CreateTrackPublication(const livekit::TrackInfo& info) override;
	std::shared_ptr<DataTrackInterface>
	CreateDataTrack(const livekit::DataTrackInfo& info) override;

private:
	bool auto_subscribe_ = true;
	PublicationHandlers handlers_;
	DataTrackSubscriptionHandler data_track_subscription_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_REMOTE_PARTICIPANT_H_
