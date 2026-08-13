/**
 *
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef _LKC_CORE_TRACK_SUBSCRIBED_QUALITY_H_
#define _LKC_CORE_TRACK_SUBSCRIBED_QUALITY_H_

#include "livekit/core/option/media_option.h"

#include <string>
#include <vector>

namespace livekit {
namespace core {

struct SubscribedQuality {
	VideoQuality quality = VideoQuality::Low;
	bool enabled = false;
};

struct SubscribedCodec {
	std::string codec;
	std::vector<SubscribedQuality> qualities;
};

// The legacy qualities field is retained for compatibility with older LiveKit servers. Newer
// servers group the requested qualities by codec in codecs.
struct SubscribedQualityUpdate {
	std::string track_sid;
	std::vector<SubscribedQuality> qualities;
	std::vector<SubscribedCodec> codecs;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_SUBSCRIBED_QUALITY_H_
