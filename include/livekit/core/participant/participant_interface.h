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

#ifndef _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_
#define _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_

#include "livekit/core/track/track_interface.h"
#include "livekit/core/track/track_publication_interface.h"

#include <map>
#include <string>

namespace livekit {
namespace core {

class ParticipantInterface {
public:
	virtual std::string Identity() = 0;
	virtual std::string Name() = 0;
	virtual std::string Sid() = 0;
	virtual bool IsSpeaking() = 0;
	virtual std::string Metadata() = 0;
	virtual std::map<std::string, std::string> Attributes() = 0;
	virtual bool IsLocalParticipant() = 0;

	virtual TrackPublicationInterface*
	GetTrackPublication(const TrackInterface::TrackSource& source) = 0;
	virtual TrackPublicationInterface* GetTrackPublicationByName(const std::string& name) = 0;
	virtual bool IsCameraEnabled() = 0;
	virtual bool IsMicrophoneEnabled() = 0;
	virtual bool IsScreenShareEnabled() = 0;
	virtual bool IsTrackPublicationEnabled(TrackPublicationInterface* publication) = 0;

public:
	virtual ~ParticipantInterface() = default;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_
