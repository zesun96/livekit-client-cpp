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

#pragma once

#ifndef _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
#define _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_

#include "livekit/core/option/option.h"

#include "../track/audio_source_interface.h"
#include "../track/local_track_interface.h"
#include "../track/track_interface.h"

#include <map>
#include <memory>
#include <string>

namespace livekit {

namespace core {

class LocalParticipantInterface {
public:
	virtual ~LocalParticipantInterface() = default;

	virtual LocalTrackInterface* CreateLocalAudioTreack(std::string label,
	                                                    AudioSourceInterface* source) = 0;

	virtual bool PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_INTERFACE_H_
