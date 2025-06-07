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

#ifndef _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_
#define _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_

#include "../detail/rtc_engine.h"
#include "livekit/core/option/room_option.h"
#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/audio_media_track_interface.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/local_track_interface.h"
#include "livekit/core/track/video_media_track_interface.h"
#include "participant.h"

namespace livekit {
namespace core {

class LocalParticipant : public Participant, public LocalParticipantInterface {
public:
	LocalParticipant(std::string sid, std::string identity, RtcEngine* engine, RoomOptions options);
	virtual ~LocalParticipant() = default;

	virtual void UpdateFromInfo(const livekit::ParticipantInfo info) override;

	virtual LocalTrackInterface* CreateLocalAudioTreack(std::string label,
	                                                    AudioSourceInterface* source) override;

	virtual bool PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) override;

private:
	RtcEngine* engine_;
	RoomOptions options_;

	// AudioSourceInterface* source_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_
