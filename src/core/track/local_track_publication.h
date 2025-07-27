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

#ifndef _LKC_CORE_TRACK_LOCAL_TRACK_PUBLICATION_H_
#define _LKC_CORE_TRACK_LOCAL_TRACK_PUBLICATION_H_

#include "livekit/core/option/option.h"
#include "livekit/core/track/local_track_publication_interface.h"
#include "local_track.h"
#include "track_publication.h"

#include "livekit_models.pb.h"

namespace livekit {
namespace core {

class LocalTrackPublication : public TrackPublication, public LocalTrackPublicationInterface {
public:
	LocalTrackPublication(livekit::TrackInfo info, LocalTrack* track);
	virtual ~LocalTrackPublication() override = default;

	void UpdatePublishOptions(TrackPublishOptions option);

private:
	TrackPublishOptions option_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_LOCAL_TRACK_PUBLICATION_H_
