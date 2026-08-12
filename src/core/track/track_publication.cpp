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
TrackPublication::TrackPublication(livekit::TrackInfo info, ::livekit::core::Track* track)
    : track_(track) {
	UpdateInfo(std::move(info));
}

std::string TrackPublication::Sid() {
	std::lock_guard<std::mutex> guard(mutex_);
	return track_sid_;
}

std::string TrackPublication::Name() {
	std::lock_guard<std::mutex> guard(mutex_);
	return track_name_;
}

TrackKind TrackPublication::Kind() {
	std::lock_guard<std::mutex> guard(mutex_);
	return kind_;
}

TrackSource TrackPublication::Source() {
	std::lock_guard<std::mutex> guard(mutex_);
	return source_;
}

TrackDimensions TrackPublication::Dimensions() {
	std::lock_guard<std::mutex> guard(mutex_);
	return dimensions_;
}

std::string TrackPublication::MimeType() {
	std::lock_guard<std::mutex> guard(mutex_);
	return mime_type_;
}

bool TrackPublication::IsMuted() {
	std::lock_guard<std::mutex> guard(mutex_);
	return muted_;
}

bool TrackPublication::IsSimulcasted() {
	std::lock_guard<std::mutex> guard(mutex_);
	return simulcasted_;
}

TrackInterface* TrackPublication::Track() {
	std::lock_guard<std::mutex> guard(mutex_);
	return track_;
}

void TrackPublication::UpdateInfo(livekit::TrackInfo info) {
	std::lock_guard<std::mutex> guard(mutex_);
	info_ = info;
	kind_ = from_proto(info.type());
	source_ = from_proto(info_.source());
	dimensions_ = {info.width(), info.height()};
	track_sid_ = info.sid();
	track_name_ = info.name();
	simulcasted_ = info.simulcast();
	mime_type_ = info.mime_type();
	muted_ = info_.muted();
}

void TrackPublication::SetTrack(::livekit::core::Track* track) {
	std::lock_guard<std::mutex> guard(mutex_);
	track_ = track;
}

void TrackPublication::SetMuted(bool muted) {
	std::lock_guard<std::mutex> guard(mutex_);
	muted_ = muted;
	info_.set_muted(muted);
	if (track_ != nullptr) {
		track_->SetMuted(muted);
	}
}

} // namespace core
} // namespace livekit
