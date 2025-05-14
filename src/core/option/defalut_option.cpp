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

#include "livekit/core/option/option.h"

namespace livekit {
namespace core {
RoomOptions default_room_options() {
	RoomOptions option;
	option.adaptive_stream = false;
	option.auto_subscribe = true;
	option.dynacast = false;
	option.join_retries = 3;
	option.rtc_config.continual_gathering_policy = ContinualGatheringPolicy::GatherContinually;
	option.rtc_config.ice_transport_type = IceTransportsType::All;
	option.sdk_options.sdk = "cpp";
	option.sdk_options.sdk_version = "0.0.1";
	return option;
}
} // namespace core
} // namespace livekit
