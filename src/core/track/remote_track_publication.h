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

#ifndef _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_
#define _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_

#include "livekit/core/track/remote_track_publication_interface.h"
#include "track_publication.h"

namespace livekit {
namespace core {

class RemoteTrackPublication : public TrackPublication, public RemoteTrackPublicationInterface {
public:
	RemoteTrackPublication() = default;
	virtual ~RemoteTrackPublication() override = default;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_REMOTE_TRACK_PUBLICATION_H_
