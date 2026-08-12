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
	virtual bool IsConnected() override;
	virtual bool Disconnect() override;
	virtual LocalParticipantInterface* GetLocalParticipant() override;
	virtual std::vector<RemoteParticipantInterface*> GetRemoteParticipants() override;
	virtual RemoteParticipantInterface* GetRemoteParticipantBySid(std::string sid) override;
	virtual RemoteParticipantInterface* GetRemoteParticipantByName(std::string name) override;
	virtual std::vector<ParticipantInterface*> Participants() override;
	virtual ParticipantInterface* GetParticipantBySid(std::string sid) override;
	virtual ParticipantInterface* GetParticipantByName(std::string name) override;

	/* Pure virtual methods inherited from RtcEngineListener */
public:
	virtual void ConnectedEvent(livekit::JoinResponse join_resp) override;
	virtual void
	ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) override;
	void MediaTrackEvent(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track) override;
	void DataPacketEvent(const livekit::DataPacket& packet) override;

private:
	void ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates);
	std::shared_ptr<RemoteParticipant> FindRemoteParticipantForTrack(const std::string& track_sid);
	void NotifyAudioFrame(const std::string& participant_sid, const std::string& track_sid,
	                      const AudioFrame& frame);
	void NotifyVideoFrame(const std::string& participant_sid, const std::string& track_sid,
	                      const VideoFrame& frame);

	struct IncomingFile {
		FileReceivedEvent event;
		uint64_t expected_length = 0;
		uint64_t next_chunk = 0;
	};

	RoomOptions options_;
	std::atomic<RoomState> state_{RoomState::Disconnected};
	std::unique_ptr<RtcEngine> rtc_engine_ = nullptr;
	std::unique_ptr<LocalParticipant> local_participant_ = nullptr;
	mutable std::mutex participants_mutex_;
	std::map<std::string, std::shared_ptr<RemoteParticipant>> remote_participants_;
	std::map<std::string, std::shared_ptr<RemoteTrack>> remote_tracks_;
	std::map<std::string, webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>>
	    pending_media_tracks_;
	std::mutex incoming_files_mutex_;
	std::map<std::string, IncomingFile> incoming_files_;
	ServerInfo server_info_;

	std::atomic<RoomEventInterface*> event_listener_{nullptr};
};

} // namespace core
} // namespace livekit

#endif //
