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

#include <mutex>
#include <string>

namespace livekit {
namespace core {
class Participant : public virtual ParticipantInterface {
public:
	Participant(std::string sid, std::string identity, std::string name, std::string metadata,
	            std::map<std::string, std::string> attributes);
	explicit Participant(const livekit::ParticipantInfo& info);
	virtual ~Participant() = default;

	virtual std::string Identity() override;
	virtual std::string Name() override;
	virtual std::string Sid() override;
	virtual bool IsSpeaking() override;
	virtual std::string Metadata() override;
	virtual std::map<std::string, std::string> Attributes() override;
	float AudioLevel() override;
	ConnectionQuality GetConnectionQuality() override;
	virtual bool IsLocalParticipant() override;

	std::vector<TrackPublicationInterface*> GetTrackPublications() override;
	TrackPublicationInterface* GetTrackPublication(const TrackSource& source) override;
	TrackPublicationInterface* GetTrackPublicationByName(const std::string& name) override;
	bool IsCameraEnabled() override;
	bool IsMicrophoneEnabled() override;
	bool IsScreenShareEnabled() override;
	bool IsTrackPublicationEnabled(TrackPublicationInterface* publication) override;

	virtual void UpdateFromInfo(const livekit::ParticipantInfo& info);

	void AddTrackPublication(std::shared_ptr<TrackPublicationInterface> publication);
	void RemoveTrackPublication(std::string track_sid);
	std::map<std::string, std::shared_ptr<TrackPublicationInterface>> TrackPublicationsSnapshot();
	bool HasTrackSid(const std::string& track_sid);
	void SetSpeakerInfo(float audio_level, bool is_speaking);
	void SetConnectionQuality(ConnectionQuality quality);

protected:
	// Updates participant identity and metadata without reconciling track publications. A full
	// reconnect needs this split so locally owned tracks survive the replacement JoinResponse and
	// can be published on the new PeerConnection.
	bool UpdateInfoFields(const livekit::ParticipantInfo& info);

	bool is_local_participant_ = false;
	mutable std::mutex participant_mutex_;
	std::string sid_;
	std::string name_;
	std::string identity_;
	std::string metadata_;
	std::map<std::string, std::string> attributes_;
	livekit::ParticipantInfo info_;
	bool is_speaking_ = false;
	std::mutex track_publications_mutex_;
	std::map<std::string, std::shared_ptr<TrackPublicationInterface>> track_publications_;
	std::map<std::string, std::shared_ptr<TrackPublicationInterface>> audio_track_publications_;
	std::map<std::string, std::shared_ptr<TrackPublicationInterface>> video_track_publications_;
	float audio_level_ = 0.0f;
	int64_t last_spoke_at_ = 0;
	livekit::ParticipantPermission permissions_;
	livekit::ParticipantInfo_Kind kind_{};
	ConnectionQuality connection_quality_ = ConnectionQuality::Unknown;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_H_
