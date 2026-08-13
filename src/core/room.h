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

#ifndef _LKC_CORE_ROOM_H_
#define _LKC_CORE_ROOM_H_

#include "livekit/core/room_interface.h"

#include "participant/local_participant.h"
#include "participant/remote_participant.h"
#include "track/remote_track.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace livekit {
namespace core {

class RtcEngine;

class Room : public RoomInterface, public RtcEngine::RtcEngineListener {
public:
	Room(RoomOptions options = default_room_options());
	virtual ~Room();

	virtual bool Connect(std::string url, std::string token,
	                     RoomConnectOptions opts = default_room_connect_options()) override;
	virtual void AddEventListener(RoomEventInterface* listener) override;
	virtual void RemoveEventListener() override;
	bool RegisterRpcMethod(std::string method, RpcHandler handler) override;
	bool UnregisterRpcMethod(const std::string& method) override;
	bool RegisterTextStreamHandler(std::string topic, TextStreamHandler handler) override;
	bool UnregisterTextStreamHandler(const std::string& topic) override;
	bool RegisterByteStreamHandler(std::string topic, ByteStreamHandler handler) override;
	bool UnregisterByteStreamHandler(const std::string& topic) override;
	RoomState State() const;
	DisconnectReason LastDisconnectReason() const;
	virtual bool IsConnected() override;
	virtual bool Disconnect() override;
	std::string Sid() override;
	std::string Name() override;
	std::string Metadata() override;
	bool IsRecording() override;
	virtual LocalParticipantInterface* GetLocalParticipant() override;
	virtual std::vector<RemoteParticipantInterface*> GetRemoteParticipants() override;
	virtual RemoteParticipantInterface* GetRemoteParticipantBySid(std::string sid) override;
	virtual RemoteParticipantInterface* GetRemoteParticipantByName(std::string name) override;
	virtual std::vector<ParticipantInterface*> Participants() override;
	virtual ParticipantInterface* GetParticipantBySid(std::string sid) override;
	virtual ParticipantInterface* GetParticipantByName(std::string name) override;
	bool SetLocalTrackMutedInternal(std::string track_sid, bool muted);
	bool SetRemoteTrackSubscribedInternal(std::string participant_sid, std::string track_sid,
	                                      bool subscribed);
	bool UpdateRemoteTrackSettingsInternal(std::string participant_sid, std::string track_sid,
	                                       const RemoteTrackSettings& settings);
	bool SimulateSignalDisconnectForTesting();
	bool SimulateFullReconnectForTesting();

	/* Pure virtual methods inherited from RtcEngineListener */
public:
	virtual void ConnectedEvent(livekit::JoinResponse join_resp) override;
	virtual void
	ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) override;
	void MediaTrackEvent(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track) override;
	void MediaTrackRemovedEvent(const std::string& track_sid) override;
	void DataPacketEvent(const livekit::DataPacket& packet) override;
	void RemoteMuteChangedEvent(const std::string& sid, bool muted) override;
	void LocalTrackUnpublishedEvent(const std::string& sid) override;
	void SpeakersChangedEvent(const std::vector<livekit::SpeakerInfo>& updates) override;
	void RoomUpdateEvent(const livekit::Room& update) override;
	void
	ConnectionQualityEvent(const std::vector<livekit::ConnectionQualityInfo>& updates) override;
	void
	SubscriptionPermissionUpdateEvent(const livekit::SubscriptionPermissionUpdate& update) override;
	void SubscriptionErrorEvent(const livekit::SubscriptionResponse& response) override;
	void StreamStateUpdateEvent(const std::vector<livekit::StreamStateInfo>& updates) override;
	void DataChannelBufferStatusEvent(const DataChannelBufferStatus& status) override;
	void SignalDisconnectedEvent(livekit::DisconnectReason reason) override;
	void ReconnectingEvent(bool full_reconnect) override;
	void SignalResumedEvent() override;
	void ResumedEvent() override;
	void ReconnectedEvent(livekit::JoinResponse join_resp) override;

private:
	void ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates,
	                             bool emit_events = true);
	void ApplyJoinResponse(const livekit::JoinResponse& join_response, bool reconnecting);
	std::shared_ptr<RemoteParticipant> FindRemoteParticipantForTrack(const std::string& track_sid);
	void NotifyAudioFrame(const std::string& participant_sid, const std::string& track_sid,
	                      const AudioFrame& frame);
	void NotifyVideoFrame(const std::string& participant_sid, const std::string& track_sid,
	                      const VideoFrame& frame);
	void NotifyDisconnectedOnce(DisconnectReason reason);
	bool SetState(RoomState state);
	bool TransitionState(RoomState expected, RoomState state);
	bool SendRemoteTrackSubscribed(const std::string& participant_sid, const std::string& track_sid,
	                               bool subscribed);
	bool SendRemoteTrackSettings(const std::string& participant_sid, const std::string& track_sid,
	                             const RemoteTrackSettings& settings);
	void RemoteSubscriptionStatusChanged(const std::string& participant_sid,
	                                     const std::string& track_sid,
	                                     TrackSubscriptionStatus status);
	RemoteParticipant::PublicationHandlers
	CreateRemotePublicationHandlers(const std::string& participant_sid);
	void ResendRemoteTrackPreferences();
	void FailIncomingDataStreams(const std::string& reason);

	struct IncomingFile {
		FileReceivedEvent event;
		ByteStreamInfo info;
		ByteStreamHandler handler;
		std::optional<uint64_t> expected_length;
		uint64_t received_length = 0;
		uint64_t next_chunk = 0;
	};
	struct IncomingText {
		TextReceivedEvent event;
		TextStreamInfo info;
		TextStreamHandler handler;
		std::optional<uint64_t> expected_length;
		uint64_t received_length = 0;
		uint64_t next_chunk = 0;
	};

	RoomOptions options_;
	std::atomic<RoomState> state_{RoomState::Disconnected};
	std::atomic<bool> disconnected_event_emitted_{false};
	std::atomic<bool> full_reconnect_prepared_{false};
	std::atomic<DisconnectReason> disconnect_reason_{DisconnectReason::Unknown};
	std::unique_ptr<RtcEngine> rtc_engine_ = nullptr;
	std::unique_ptr<LocalParticipant> local_participant_ = nullptr;
	mutable std::mutex participants_mutex_;
	std::map<std::string, std::shared_ptr<RemoteParticipant>> remote_participants_;
	std::map<std::string, std::shared_ptr<RemoteTrack>> remote_tracks_;
	std::map<std::string, webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>>
	    pending_media_tracks_;
	std::mutex incoming_streams_mutex_;
	std::map<std::string, IncomingFile> incoming_files_;
	std::map<std::string, IncomingText> incoming_texts_;
	std::mutex transcription_mutex_;
	std::map<std::string, int64_t> transcription_received_times_;
	std::mutex stream_handlers_mutex_;
	std::map<std::string, TextStreamHandler> text_stream_handlers_;
	std::map<std::string, ByteStreamHandler> byte_stream_handlers_;
	ServerInfo server_info_;
	mutable std::mutex room_info_mutex_;
	livekit::Room room_info_;

	std::atomic<RoomEventInterface*> event_listener_{nullptr};
};

} // namespace core
} // namespace livekit

#endif //
