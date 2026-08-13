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

#ifndef _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_
#define _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_

#include "../detail/rtc_engine.h"
#include "../track/local_track_publication.h"
#include "livekit/core/option/option.h"
#include "livekit/core/participant/local_participant_interface.h"
#include "livekit/core/track/audio_media_track_interface.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/local_track_interface.h"
#include "livekit/core/track/video_media_track_interface.h"
#include "participant.h"

#include <atomic>
#include <mutex>
#include <set>

namespace livekit {
namespace core {

class RoomEventInterface;
class OutgoingDataStreamState;

class LocalParticipant : public Participant, public LocalParticipantInterface {
public:
	LocalParticipant(std::string sid, std::string identity, EncryptionType encryption_type,
	                 RtcEngine* engine, RoomOptions options);
	~LocalParticipant() override;

	virtual void UpdateFromInfo(const livekit::ParticipantInfo& info) override;
	void UpdateFromInfoPreservingTracks(const livekit::ParticipantInfo& info);

	virtual LocalTrackInterface* CreateLocalAudioTreack(std::string label,
	                                                    AudioSourceInterface* source) override;
	LocalTrackInterface* CreateLocalVideoTrack(std::string label,
	                                           VideoSourceInterface* source) override;

	virtual bool PublishTrack(LocalTrackInterface* track, TrackPublishOptions option) override;
	bool UnpublishTrack(LocalTrackInterface* track, bool stop_on_unpublish) override;
	std::size_t UnpublishTracks(const std::vector<LocalTrackInterface*>& tracks,
	                            bool stop_on_unpublish) override;
	bool RepublishAllTracks() override;
	// Recreates publications after the underlying PeerConnection has been replaced during a full
	// reconnect. Unlike RepublishAllTracks(), this does not try to remove senders from the old PC.
	void DetachTrackTransceiversForReconnect();
	bool RepublishAllTracksAfterReconnect();
	bool SetMetadata(const std::string& metadata) override;
	bool SetName(const std::string& name) override;
	bool SetAttributes(const std::map<std::string, std::string>& attributes) override;
	bool PublishData(const std::vector<uint8_t>& data, DataPublishOptions options) override;
	bool PublishDtmf(uint32_t code, std::string digit) override;
	std::optional<ChatMessage> SendChatMessage(std::string message) override;
	std::optional<ChatMessage> EditChatMessage(std::string message,
	                                           const ChatMessage& original) override;
	bool SendText(const std::string& text, TextSendOptions options) override;
	bool SendBytes(const std::vector<uint8_t>& data, ByteSendOptions options) override;
	bool SendFile(const std::string& path, FileSendOptions options) override;
	std::unique_ptr<TextStreamWriterInterface> StreamText(StreamTextOptions options) override;
	std::unique_ptr<ByteStreamWriterInterface> StreamBytes(StreamBytesOptions options) override;
	RpcResult PerformRpc(const PerformRpcParams& params) override;
	bool SetTrackSubscriptionPermissions(
	    bool all_participants_allowed,
	    const std::vector<ParticipantTrackPermission>& participant_permissions) override;
	bool ResendTrackSubscriptionPermissions();
	void SetEventListener(RoomEventInterface* listener);
	void LocalTrackSubscribed(const std::string& track_sid);

private:
	RtcEngine* engine_;
	RoomOptions options_;
	EncryptionType encryption_type_;
	std::atomic<RoomEventInterface*> event_listener_{nullptr};
	std::mutex subscription_permissions_mutex_;
	bool all_participants_allowed_to_subscribe_ = true;
	std::vector<ParticipantTrackPermission> participant_track_permissions_;
	std::shared_ptr<OutgoingDataStreamState> outgoing_stream_state_;
	std::mutex local_track_subscriptions_mutex_;
	std::set<std::string> subscribed_local_track_sids_;
	std::set<std::string> emitted_local_track_subscriptions_;

	// AudioSourceInterface* source_;
};
} // namespace core
} // namespace livekit

#endif // _LKC_CORE_PARTICIPANT_LOCAL_PARTICIPANT_H_
