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

#pragma once

#ifndef _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_
#define _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_

#include "livekit/core/track/track.h"
#include "livekit/core/track/track_publication.h"

#include <string>

namespace livekit {
namespace core {

class ParticipantInterface {
public:
	virtual std::string Identity() const = 0;
	virtual std::string Name() const = 0;
	virtual std::string Sid() const = 0;
	virtual bool IsSpeaking() const = 0;
	virtual std::string Metadata() const = 0;
	virtual std::string Attributes() const = 0;

	virtual TrackPublication* GetTrackPublication(const Track::Source& source) = 0;
	virtual TrackPublication* GetTrackPublicationByName(const std::string& name) = 0;
	virtual bool IsCameraEnabled() const = 0;
	virtual bool IsMicrophoneEnabled() const = 0;
	virtual bool IsScreenShareEnabled() const = 0;
	virtual bool IsTrackPublicationEnabled(TrackPublication* publication) const = 0;

protected:
	virtual ~ParticipantInterface() = default;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_PARTICIPANT_INTERFACE_H_
