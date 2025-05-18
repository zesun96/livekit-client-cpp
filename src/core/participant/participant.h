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

#include "livekit_models.pb.h"
#include "livekit_rtc.pb.h"
#include "livekit/core/participant/participant_interface.h"

#include <string>

namespace livekit {
namespace core {
class Participant : public ParticipantInterface {
public:
	Participant() = default;
	virtual ~Participant() = default;

	virtual void UpdateFromInfo(const livekit::ParticipantInfo info);
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_H_
