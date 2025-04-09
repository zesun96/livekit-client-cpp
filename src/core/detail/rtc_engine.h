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

#ifndef _LKC_CORE_DETAIL_RTC_ENGINE_H_
#define _LKC_CORE_DETAIL_RTC_ENGINE_H_

#include "livekit/core/option/rtc_engine_option.h"
#include "livekit_rtc.pb.h"
#include "rtc_session.h"
#include "signal_client.h"

#include <memory>
#include <string>

namespace livekit {
namespace core {

class RtcSession;

class SignalClient;
class RtcEngine : public SignalClientObserver {
public:
	RtcEngine();
	~RtcEngine();

	livekit::JoinResponse Connect(std::string url, std::string token, EngineOptions options);

	/* Pure virtual methods inherited from SignalClientObserver */
public:
	virtual void OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) override;
	virtual void OnLeave(const livekit::LeaveRequest leave) override;
	virtual void OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) override;
	virtual void
	OnLocalTrackUnpublished(const livekit::TrackUnpublishedResponse& response) override;
	virtual void OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) override;
	virtual void OnRemoteMuteChanged(std::string sid, bool muted) override;
	virtual void OnSubscribedQualityUpdate(const livekit::SubscribedQualityUpdate& update) override;
	virtual void OnTokenRefresh(const std::string& token) override;
	virtual void OnTrickle(std::string& candidate, livekit::SignalTarget target) override;
	virtual void OnClose() override;
	virtual void OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) override;
	virtual void OnSpeakersChanged(std::vector<livekit::SpeakerInfo>& update) override;
	virtual void OnRoomUpdate(const livekit::Room& update) override;
	virtual void
	OnConnectionQuality(const std::vector<livekit::ConnectionQualityInfo>& update) override;
	virtual void OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) override;
	virtual void
	OnSubscriptionPermissionUpdate(const livekit::SubscriptionPermissionUpdate& update) override;
	virtual void OnSubscriptionError(const livekit::SubscriptionResponse& response) override;
	virtual void OnRequestResponse(const livekit::RequestResponse& response) override;
	virtual void OnLocalTrackSubscribed(const std::string& track_sid) override;

private:
	std::unique_ptr<SignalClient> signal_client_;
	std::unique_ptr<RtcSession> rtc_session_;
};

} // namespace core
} // namespace livekit

#endif //
