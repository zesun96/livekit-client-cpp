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

#include "local_track_publication.h"

#include <utility>

namespace livekit {
namespace core {

LocalTrackPublication::LocalTrackPublication(livekit::TrackInfo info, LocalTrack* track)
    : TrackPublication(info, track) {}

void LocalTrackPublication::UpdatePublishOptions(TrackPublishOptions option) {
	std::lock_guard<std::mutex> guard(option_mutex_);
	option_ = option;
}

TrackPublishOptions LocalTrackPublication::PublishOptions() const {
	std::lock_guard<std::mutex> guard(option_mutex_);
	return option_;
}

void LocalTrackPublication::UpdateSubscribedQuality(SubscribedQualityUpdate update) {
	std::lock_guard<std::mutex> guard(subscribed_quality_mutex_);
	subscribed_quality_update_ = std::move(update);
}

std::optional<SubscribedQualityUpdate> LocalTrackPublication::LastSubscribedQualityUpdate() const {
	std::lock_guard<std::mutex> guard(subscribed_quality_mutex_);
	return subscribed_quality_update_;
}

} // namespace core
} // namespace livekit
