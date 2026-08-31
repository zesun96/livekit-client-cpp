/**
 *
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef _LKC_CORE_PARTICIPANT_PARTICIPANT_SNAPSHOT_H_
#define _LKC_CORE_PARTICIPANT_PARTICIPANT_SNAPSHOT_H_

#include "livekit/core/option/e2ee_option.h"
#include "livekit/core/participant/participant_interface.h"
#include "livekit/core/track/rtc_stats.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace livekit {
namespace core {

struct RemoteTrackSnapshot {
	std::string sid;
	std::string name;
	TrackKind kind = TrackKind::Unknown;
	TrackSource source = TrackSource::Unknown;
	TrackStreamState stream_state = TrackStreamState::Unknown;
	TrackDimensions dimensions;
	bool enabled = false;
	RTCStatsSnapshot rtc_stats;
};

struct RemoteTrackPublicationSnapshot {
	std::string sid;
	std::string name;
	std::string mime_type;
	TrackKind kind = TrackKind::Unknown;
	TrackSource source = TrackSource::Unknown;
	TrackDimensions dimensions;
	bool muted = false;
	bool simulcasted = false;
	bool subscription_allowed = true;
	TrackSubscriptionStatus subscription_status = TrackSubscriptionStatus::Unsubscribed;
	std::optional<SubscriptionError> subscription_error;
	std::optional<RemoteTrackSnapshot> subscribed_track;
	EncryptionType encryption = EncryptionType::None;
};

struct RemoteParticipantSnapshot {
	std::string sid;
	std::string identity;
	std::string name;
	std::string metadata;
	std::map<std::string, std::string> attributes;
	float audio_level = 0.0f;
	ConnectionQuality connection_quality = ConnectionQuality::Unknown;
	bool speaking = false;
	ParticipantPermissions permissions;
	std::vector<RemoteTrackPublicationSnapshot> publications;
};

struct LocalParticipantSnapshot {
	std::string sid;
	std::string identity;
	std::string name;
	std::string metadata;
	std::map<std::string, std::string> attributes;
	float audio_level = 0.0f;
	ConnectionQuality connection_quality = ConnectionQuality::Unknown;
	bool speaking = false;
	ParticipantPermissions permissions;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_PARTICIPANT_SNAPSHOT_H_
