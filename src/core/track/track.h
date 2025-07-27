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

#include "livekit_models.pb.h"

#include <api/rtp_transceiver_interface.h>

#include <atomic>

namespace livekit {
namespace core {
class Track : public TrackInterface {
public:
public:
	Track(std::string sid, std::string name, TrackKind kind);
	virtual ~Track() = default;

	virtual std::string Sid() override;
	virtual std::string Name() override;
	virtual TrackKind Kind() override;
	virtual TrackSource Source() override;
	virtual TrackStreamState StreamState() override;
	virtual TrackDimensions Dimensions() override;

	bool Muted();

	void SetMuted(bool muted);

	std::string GetRTCStats() override;
	void SetEnabled(bool enabled) override;
	bool IsEnabled() override;

	void UpdateInfo(livekit::TrackInfo info);

	void SetTransceiver(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver);

private:
	livekit::TrackInfo info_;
	std::string sid_;
	std::string name_;
	TrackKind kind_;
	TrackSource source_;
	TrackStreamState stream_state_;
	TrackDimensions dimensions_;
	std::atomic<bool> muted_;
	rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_TRACK_TRACK_H_