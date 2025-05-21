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

namespace livekit {
namespace core {
Participant::Participant(std::string sid, std::string identity, std::string name,
                         std::string metadata, std::map<std::string, std::string> attributes)
    : sid_(sid), identity_(identity), name_(name), metadata_(metadata), attributes_(attributes) {}

void Participant::UpdateFromInfo(const livekit::ParticipantInfo info) {}

} // namespace core
} // namespace livekit