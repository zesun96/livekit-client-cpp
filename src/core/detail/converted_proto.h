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

#ifndef _LKC_CORE_CONVERTED_PROTO_H_
#define _LKC_CORE_CONVERTED_PROTO_H_

#include "livekit/core/option/option.h"
#include "livekit/core/protostruct/livekit_rtc_struct.h"
#include "livekit/core/track/track_interface.h"
#include "livekit_models.pb.h"
#include "livekit_rtc.pb.h"

namespace livekit {
namespace core {

ProtoJoinResponse from_proto(livekit::JoinResponse proto);
livekit::JoinResponse to_proto(ProtoJoinResponse src);

ServerInfo from_proto(livekit::ServerInfo proto);
livekit::ServerInfo to_proto(ServerInfo src);

TrackKind from_proto(livekit::TrackType proto);
livekit::TrackType to_proto(TrackKind src);

TrackSource from_proto(livekit::TrackSource proto);
livekit::TrackSource to_proto(TrackSource src);

EncryptionType from_proto(livekit::Encryption_Type proto);
livekit::Encryption_Type to_proto(EncryptionType src);

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_CONVERTED_PROTO_H_
