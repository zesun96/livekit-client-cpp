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

#include "track.h"
#include "../detail/converted_proto.h"

namespace livekit {
namespace core {
Track::Track(std::string sid, std::string name, TrackKind kind)
    : sid_(sid), name_(name), kind_(kind), dimensions_{0, 0}, source_(TrackSource::Unknown),
      stream_state_(TrackStreamState::Unknown) {
	info_ = livekit::TrackInfo();
	info_.set_sid(sid);
	info_.set_name(name);
	info_.set_type(to_proto(kind));
}

std::string Track::Sid() { return sid_; };
std::string Track::Name() { return name_; };
TrackKind Track::Kind() { return kind_; };
TrackSource Track::Source() { return source_; };
TrackStreamState Track::StreamState() { return stream_state_; };
TrackDimensions Track::Dimensions() { return dimensions_; };

bool Track::Muted() { return muted_.load(); };

void Track::SetMuted(bool muted) { muted_.store(muted); };

std::string Track::GetRTCStats() { return ""; };
void Track::Track::SetEnabled(bool enabled) {};
bool Track::IsEnabled() { return true; };

void Track::UpdateInfo(livekit::TrackInfo info) {
	info_ = info;
	kind_ = from_proto(info.type());
	source_ = from_proto(info_.source());
	name_ = info_.name();
	sid_ = info_.sid();
}

void Track::SetTransceiver(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
	transceiver_ = transceiver;
}

} // namespace core
} // namespace livekit
