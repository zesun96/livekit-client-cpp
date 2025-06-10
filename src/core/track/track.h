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

#ifndef _LKC_CORE_TRACK_TRACK_H_
#define _LKC_CORE_TRACK_TRACK_H_

#include "livekit/core/track/track_interface.h"

#include <atomic>

namespace livekit {
namespace core {
class Track : public TrackInterface {
public:
public:
	Track() = default;
	virtual ~Track() = default;

	virtual std::string Sid() override { return sid_; };
	virtual std::string Name() override { return name_; };
	virtual TrackKind Kind() override { return kind_; };
	virtual TrackSource Source() override { return source_; };
	virtual TrackStreamState StreamState() override { return stream_state_; };
	virtual TrackDimensions Dimensions() override { return dimensions_; };

	bool Muted() { return muted_.load(); };

	void SetMuted(bool muted) { muted_.store(muted); };

	std::string GetRTCStats() override { return ""; };
	void SetEnabled(bool enabled) override {};
	bool IsEnabled() override { return true; };

private:
	std::string sid_;
	std::string name_;
	TrackKind kind_;
	TrackSource source_;
	TrackStreamState stream_state_;
	TrackDimensions dimensions_;
	std::atomic<bool> muted_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_TRACK_H_