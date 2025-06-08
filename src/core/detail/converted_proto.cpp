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

#include "converted_proto.h"

namespace livekit {
namespace core {
ProtoJoinResponse from_proto(livekit::JoinResponse proto) {
	ProtoJoinResponse join_response;
	join_response.room.name = proto.room().name();
	return join_response;
}

livekit::JoinResponse to_proto(ProtoJoinResponse src) { return livekit::JoinResponse(); }

ServerInfo from_proto(livekit::ServerInfo proto) {
	ServerInfo server_info;
	server_info.node_id = proto.node_id();
	return server_info;
}

livekit::ServerInfo to_proto(ServerInfo src) { return livekit::ServerInfo(); }

TrackKind from_proto(livekit::TrackType proto) {
	switch (proto) {
	case livekit::TrackType::AUDIO:
		return TrackKind::Audio;
	case livekit::TrackType::VIDEO:
		return TrackKind::Video;
	default:
		return TrackKind::Unknown;
	}
}

livekit::TrackType to_proto(TrackKind src) {
	switch (src) {
	case TrackKind::Audio:
		return livekit::TrackType::AUDIO;
	case TrackKind::Video:
		return livekit::TrackType::VIDEO;
	}
}

TrackSource from_proto(livekit::TrackSource proto) {
	switch (proto) {
	case livekit::TrackSource::MICROPHONE:
		return TrackSource::Microphone;
	case livekit::TrackSource::CAMERA:
		return TrackSource::Camera;
	case livekit::TrackSource::SCREEN_SHARE:
		return TrackSource::ScreenShare;
	case livekit::TrackSource::SCREEN_SHARE_AUDIO:
		return TrackSource::ScreenShareAudio;
	default:
		return TrackSource::Unknown;
	}
}

livekit::TrackSource to_proto(TrackSource src) {
	switch (src) {
	case TrackSource::Microphone:
		return livekit::TrackSource::MICROPHONE;
	case TrackSource::Camera:
		return livekit::TrackSource::CAMERA;
	case TrackSource::ScreenShare:
		return livekit::TrackSource::SCREEN_SHARE;
	case TrackSource::ScreenShareAudio:
		return livekit::TrackSource::SCREEN_SHARE_AUDIO;
	default:
		return livekit::TrackSource::UNKNOWN;
	}
}

EncryptionType from_proto(livekit::Encryption_Type proto) {
	switch (proto) {
	case livekit::Encryption_Type::Encryption_Type_NONE:
		return EncryptionType::None;
	case livekit::Encryption_Type::Encryption_Type_GCM:
		return EncryptionType::Gcm;
	case livekit::Encryption_Type::Encryption_Type_CUSTOM:
		return EncryptionType::Custom;
	default:
		return EncryptionType::None;
	}
}

livekit::Encryption_Type to_proto(EncryptionType src) {
	switch (src) {
	case EncryptionType::None:
		return livekit::Encryption_Type::Encryption_Type_NONE;
	case EncryptionType::Gcm:
		return livekit::Encryption_Type::Encryption_Type_GCM;
	case EncryptionType::Custom:
		return livekit::Encryption_Type::Encryption_Type_CUSTOM;
	default:
		return livekit::Encryption_Type::Encryption_Type_NONE;
	}
}

} // namespace core
} // namespace livekit
