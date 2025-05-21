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

#ifndef _LKC_CORE_PARTICIPANT_H_
#define _LKC_CORE_PARTICIPANT_H_

#include "livekit/core/participant/participant_interface.h"
#include "livekit_models.pb.h"
#include "livekit_rtc.pb.h"

#include <string>

namespace livekit {
namespace core {
class Participant : public ParticipantInterface {
public:
	Participant(std::string sid, std::string identity, std::string name, std::string metadata,
	            std::map<std::string, std::string> attributes);
	virtual ~Participant() = default;

	virtual std::string Identity() override { return identity_; }
	virtual std::string Name() override { return name_; }
	virtual std::string Sid() override { return sid_; }
	virtual bool IsSpeaking() override { return is_speaking_; }
	virtual std::string Metadata() override { return metadata_; }
	virtual std::map<std::string, std::string> Attributes() override { return attributes_; }
	virtual bool IsLocalParticipant() override { return is_local_particitant_; }

	virtual TrackPublicationInterface*
	GetTrackPublication(const TrackInterface::TrackSource& source) override {
		return nullptr;
	};
	virtual TrackPublicationInterface* GetTrackPublicationByName(const std::string& name) override {
		return nullptr;
	};
	virtual bool IsCameraEnabled() override { return false; };
	virtual bool IsMicrophoneEnabled() override { return false; };
	virtual bool IsScreenShareEnabled() override { return false; };
	virtual bool IsTrackPublicationEnabled(TrackPublicationInterface* publication) override {
		return false;
	};

	virtual void UpdateFromInfo(const livekit::ParticipantInfo info);

protected:
	bool is_local_particitant_ = false;
	std::string sid_;
	std::string name_;
	std::string identity_;
	std::string metadata_;
	std::map<std::string, std::string> attributes_;
	livekit::ParticipantInfo info_;
	bool is_speaking_ = false;
	std::map<std::string, TrackPublicationInterface*> track_publications_;
	std::map<std::string, TrackPublicationInterface*> audio_track_publications_;
	std::map<std::string, TrackPublicationInterface*> video_track_publications_;
	float audio_level_ = 0.0f;
	int64_t last_spoke_at_ = 0;
	livekit::ParticipantPermission permissions_;
	livekit::ParticipantInfo_Kind kind_;
	livekit::ConnectionQuality connection_quality_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_H_
