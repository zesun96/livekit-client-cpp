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

#pragma once

#ifndef _LKC_CORE_TRACK_AUDIO_FRAME_INTERFACE_H_
#define _LKC_CORE_TRACK_AUDIO_FRAME_INTERFACE_H_

#include <cstdint>
#include <vector>

namespace livekit {
namespace core {

// Interleaved signed 16-bit PCM samples.
struct AudioFrame {
	std::vector<int16_t> data;
	uint32_t sample_rate = 0;
	uint32_t num_channels = 0;
	uint32_t samples_per_channel = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_AUDIO_FRAME_INTERFACE_H_
