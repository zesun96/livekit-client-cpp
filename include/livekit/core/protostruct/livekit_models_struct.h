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

#ifndef _LKC_CORE_PROTOSTRUCT_LIVEKIT_MODELS_STRUCT_H_
#define _LKC_CORE_PROTOSTRUCT_LIVEKIT_MODELS_STRUCT_H_

#include <string>

namespace livekit {
namespace core {
struct ProtoRoom {
	std::string sid;
	std::string name;
};

enum Edition { Standard, Cloud };

struct ServerInfo {
	Edition edition;
	std::string version;
	int32_t protocol;
	std::string region;
	std::string node_id;
	// additional debugging information. sent only if server is in development mode
	std::string debug_info;
	std::int32_t agent_protocol;
};

} // namespace core
} // namespace livekit

#endif //
