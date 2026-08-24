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

#include "participant.h"

#include "../data_track.h"
#include "../detail/converted_proto.h"
#include "../detail/data_track_proto.h"
#include "../track/track_publication.h"

#include <set>
#include <utility>

namespace livekit {
namespace core {
Participant::Participant(std::string sid, std::string identity, std::string name,
                         std::string metadata, std::map<std::string, std::string> attributes)
    : sid_(std::move(sid)), name_(std::move(name)), identity_(std::move(identity)),
      metadata_(std::move(metadata)), attributes_(std::move(attributes)) {}

Participant::Participant(const livekit::ParticipantInfo& info) { UpdateFromInfo(info); }

std::string Participant::Identity() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return identity_;
}

std::string Participant::Name() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return name_;
}

std::string Participant::Sid() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return sid_;
}

bool Participant::IsSpeaking() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return is_speaking_;
}

std::string Participant::Metadata() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return metadata_;
}

std::map<std::string, std::string> Participant::Attributes() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return attributes_;
}

float Participant::AudioLevel() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return audio_level_;
}

ConnectionQuality Participant::GetConnectionQuality() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return connection_quality_;
}

ParticipantPermissions Participant::Permissions() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return permissions_;
}

bool Participant::IsLocalParticipant() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return is_local_participant_;
}

void Participant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	if (!UpdateInfoFields(info)) {
		return;
	}

	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	std::set<std::string> current_sids;
	for (const auto& track_info : info.tracks()) {
		if (track_info.sid().empty()) {
			continue;
		}
		current_sids.insert(track_info.sid());
		auto publication = track_publications_.find(track_info.sid());
		if (publication == track_publications_.end()) {
			track_publications_.emplace(track_info.sid(), CreateTrackPublication(track_info));
		} else if (auto* concrete = dynamic_cast<TrackPublication*>(publication->second.get())) {
			concrete->UpdateInfo(track_info);
		}
	}
	for (auto publication = track_publications_.begin();
	     publication != track_publications_.end();) {
		if (current_sids.count(publication->first) == 0) {
			publication = track_publications_.erase(publication);
		} else {
			++publication;
		}
	}
	{
		std::lock_guard<std::mutex> data_guard(data_tracks_mutex_);
		std::set<std::string> current_data_sids;
		for (const auto& data_info : info.data_tracks()) {
			if (data_info.sid().empty()) {
				continue;
			}
			current_data_sids.insert(data_info.sid());
			auto found = data_tracks_.find(data_info.sid());
			if (found == data_tracks_.end() && data_info.pub_handle() != 0) {
				for (auto candidate = data_tracks_.begin(); candidate != data_tracks_.end();
				     ++candidate) {
					if (candidate->second->Info().publisher_handle == data_info.pub_handle()) {
						auto preserved = candidate->second;
						data_tracks_.erase(candidate);
						found = data_tracks_.emplace(data_info.sid(), std::move(preserved)).first;
						break;
					}
				}
			}
			if (found == data_tracks_.end()) {
				auto created = CreateDataTrack(data_info);
				if (created) {
					data_tracks_.emplace(data_info.sid(), std::move(created));
				}
			} else if (auto* remote = dynamic_cast<RemoteDataTrack*>(found->second.get())) {
				remote->UpdateInfo(detail::FromProto(data_info));
			} else if (auto* local = dynamic_cast<LocalDataTrack*>(found->second.get())) {
				local->UpdateInfo(detail::FromProto(data_info));
			}
		}
		for (auto track = data_tracks_.begin(); track != data_tracks_.end();) {
			if (current_data_sids.count(track->first) == 0) {
				track = data_tracks_.erase(track);
			} else {
				++track;
			}
		}
	}
}

std::shared_ptr<TrackPublicationInterface>
Participant::CreateTrackPublication(const livekit::TrackInfo& info) {
	return std::make_shared<TrackPublication>(info, nullptr);
}

std::shared_ptr<DataTrackInterface> Participant::CreateDataTrack(const livekit::DataTrackInfo&) {
	return nullptr;
}

bool Participant::UpdateInfoFields(const livekit::ParticipantInfo& info) {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	if (!info_.sid().empty() && info_.sid() == info.sid() && info_.version() > info.version()) {
		return false;
	}
	info_ = info;
	sid_ = info.sid();
	identity_ = info.identity();
	name_ = info.name();
	metadata_ = info.metadata();
	attributes_.clear();
	for (const auto& [key, value] : info.attributes()) {
		attributes_.emplace(key, value);
	}
	permissions_.can_subscribe = info.permission().can_subscribe();
	permissions_.can_publish = info.permission().can_publish();
	permissions_.can_publish_data = info.permission().can_publish_data();
	permissions_.can_publish_sources.clear();
	permissions_.can_publish_sources.reserve(
	    static_cast<std::size_t>(info.permission().can_publish_sources_size()));
	for (const auto source : info.permission().can_publish_sources()) {
		permissions_.can_publish_sources.push_back(
		    from_proto(static_cast<livekit::TrackSource>(source)));
	}
	permissions_.hidden = info.permission().hidden();
	permissions_.recorder = info.permission().recorder();
	permissions_.can_update_metadata = info.permission().can_update_metadata();
	permissions_.agent = info.permission().agent();
	permissions_.can_subscribe_metrics = info.permission().can_subscribe_metrics();
	permissions_.can_manage_agent_session = info.permission().can_manage_agent_session();
	kind_ = info.kind();
	return true;
}

std::vector<TrackPublicationInterface*> Participant::GetTrackPublications() {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	std::vector<TrackPublicationInterface*> publications;
	publications.reserve(track_publications_.size());
	for (const auto& [sid, publication] : track_publications_) {
		publications.push_back(publication.get());
	}
	return publications;
}

TrackPublicationInterface* Participant::GetTrackPublication(const TrackSource& source) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	for (const auto& [sid, publication] : track_publications_) {
		if (publication->Source() == source) {
			return publication.get();
		}
	}
	return nullptr;
}

TrackPublicationInterface* Participant::GetTrackPublicationByName(const std::string& name) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	for (const auto& [sid, publication] : track_publications_) {
		if (publication->Name() == name) {
			return publication.get();
		}
	}
	return nullptr;
}

bool Participant::IsCameraEnabled() {
	return IsTrackPublicationEnabled(GetTrackPublication(TrackSource::Camera));
}

bool Participant::IsMicrophoneEnabled() {
	return IsTrackPublicationEnabled(GetTrackPublication(TrackSource::Microphone));
}

bool Participant::IsScreenShareEnabled() {
	return GetTrackPublication(TrackSource::ScreenShare) != nullptr;
}

bool Participant::IsTrackPublicationEnabled(TrackPublicationInterface* publication) {
	return publication != nullptr && !publication->IsMuted();
}

void Participant::AddTrackPublication(std::shared_ptr<TrackPublicationInterface> publication) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	track_publications_[publication->Sid()] = publication;
}

void Participant::RemoveTrackPublication(std::string track_sid) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	track_publications_.erase(track_sid);
}

std::map<std::string, std::shared_ptr<TrackPublicationInterface>>
Participant::TrackPublicationsSnapshot() {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	return track_publications_;
}

std::vector<DataTrackInterface*> Participant::GetDataTracks() {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	std::vector<DataTrackInterface*> tracks;
	tracks.reserve(data_tracks_.size());
	for (const auto& [sid, track] : data_tracks_) {
		tracks.push_back(track.get());
	}
	return tracks;
}

DataTrackInterface* Participant::GetDataTrackBySid(const std::string& sid) {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	auto found = data_tracks_.find(sid);
	return found == data_tracks_.end() ? nullptr : found->second.get();
}

DataTrackInterface* Participant::GetDataTrackByName(const std::string& name) {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	for (const auto& [sid, track] : data_tracks_) {
		if (track->Info().name == name) {
			return track.get();
		}
	}
	return nullptr;
}

void Participant::AddDataTrack(std::shared_ptr<DataTrackInterface> track) {
	if (!track) {
		return;
	}
	const auto info = track->Info();
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	data_tracks_[info.sid] = std::move(track);
}

void Participant::RemoveDataTrack(const std::string& sid) {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	data_tracks_.erase(sid);
}

std::map<std::string, std::shared_ptr<DataTrackInterface>> Participant::DataTracksSnapshot() {
	std::lock_guard<std::mutex> guard(data_tracks_mutex_);
	return data_tracks_;
}

void Participant::SetSpeakerInfo(float audio_level, bool is_speaking) {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	audio_level_ = audio_level;
	is_speaking_ = is_speaking;
}

void Participant::SetConnectionQuality(ConnectionQuality quality) {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	connection_quality_ = quality;
}

bool Participant::HasTrackSid(const std::string& track_sid) {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	for (const auto& track : info_.tracks()) {
		if (track.sid() == track_sid) {
			return true;
		}
	}
	return false;
}

} // namespace core
} // namespace livekit
