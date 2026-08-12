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

bool Participant::IsLocalParticipant() {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	return is_local_participant_;
}

void Participant::UpdateFromInfo(const livekit::ParticipantInfo& info) {
	std::lock_guard<std::mutex> guard(participant_mutex_);
	info_ = info;
	sid_ = info.sid();
	identity_ = info.identity();
	name_ = info.name();
	metadata_ = info.metadata();
	attributes_.clear();
	for (const auto& [key, value] : info.attributes()) {
		attributes_.emplace(key, value);
	}
	permissions_ = info.permission();
	kind_ = info.kind();
}

void Participant::AddTrackPublication(std::shared_ptr<TrackPublicationInterface> publication) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	track_publications_[publication->Sid()] = publication;
}

void Participant::RemoveTrackPublication(std::string track_sid) {
	std::lock_guard<std::mutex> guard(track_publications_mutex_);
	track_publications_.erase(track_sid);
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
