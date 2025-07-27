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

#include "track_publication.h"
#include "../detail/converted_proto.h"

namespace livekit {
namespace core {
TrackPublication::TrackPublication(livekit::TrackInfo info, Track* track)
    : info_(info), track_(track), kind_(from_proto(info.type())),
      source_(from_proto(info_.source())), dimensions_{info.width(), info_.height()},
      track_sid_(info.sid()), simulcasted_(info.simulcast()), mime_type_(info.mime_type()),
      muted_(info_.muted()) {}

std::string TrackPublication::Sid() { return track_sid_; }

void TrackPublication::UpdateInfo(livekit::TrackInfo info) {
	info_ = info;
	kind_ = from_proto(info.type());
	source_ = from_proto(info_.source());
	dimensions_ = {info.width(), info.height()};
	track_sid_ = info.sid();
	simulcasted_ = info.simulcast();
	mime_type_ = info.mime_type();
	muted_ = info_.muted();
}

} // namespace core
} // namespace livekit
