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

#ifndef _LKC_CORE_TRACK_TRACK_INTERFACE_H_
#define _LKC_CORE_TRACK_TRACK_INTERFACE_H_

#include "livekit/core/option/option.h"

#include <string>

namespace livekit {
namespace core {

class TrackInterface {
public:
	virtual std::string GetRTCStats() = 0;
	virtual void SetEnabled(bool enabled) = 0;
	virtual bool IsEnabled() = 0;
	virtual TrackKind Kind() = 0;
	virtual TrackSource Source() = 0;
	virtual std::string Sid() = 0;
	virtual std::string Name() = 0;
	virtual TrackStreamState StreamState() = 0;
	virtual TrackDimensions Dimensions() = 0;

protected:
	virtual ~TrackInterface() = default;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_TRACK_INTERFACE_H_
