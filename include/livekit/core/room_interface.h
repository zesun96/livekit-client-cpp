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

#ifndef _LKC_CORE_ROOM_INTERFACE_H_
#define _LKC_CORE_ROOM_INTERFACE_H_

#include "media_device.h"
#include "option/room_option.h"
#include "participant/local_participant_interface.h"
#include "participant/participant_interface.h"
#include "participant/participant_snapshot.h"
#include "participant/remote_participant_interface.h"
#include "protostruct/livekit_rtc_struct.h"
#include "room_event_interface.h"
#include "rpc.h"

#include <memory>

namespace livekit {
namespace core {

class E2EEManager;

class RoomInterface {
public:
	using RoomState = core::RoomState;

	virtual ~RoomInterface() = default;

	virtual bool Connect(std::string url, std::string token,
	                     RoomConnectOptions opts = default_room_connect_options()) = 0;

	virtual void AddEventListener(RoomEventInterface* listener) = 0;
	virtual void RemoveEventListener() = 0;
	virtual bool RegisterRpcMethod(std::string method, RpcHandler handler) = 0;
	virtual bool UnregisterRpcMethod(const std::string& method) = 0;

	// Returns the full connection lifecycle state. Unlike IsConnected(), this distinguishes
	// connecting, disconnecting, and failed rooms.
	RoomState State() const;
	DisconnectReason LastDisconnectReason() const;
	virtual bool IsConnected() = 0;
	virtual bool Disconnect() = 0;
	virtual std::string Sid() { return {}; }
	virtual std::string Name() { return {}; }
	virtual std::string Metadata() { return {}; }
	virtual bool IsRecording() { return false; }

	virtual LocalParticipantInterface* GetLocalParticipant() = 0;
	virtual std::vector<RemoteParticipantInterface*> GetRemoteParticipants() = 0;
	// Returns immutable values that remain valid independently of subsequent room updates.
	std::vector<RemoteParticipantSnapshot> GetRemoteParticipantSnapshots() const;
	virtual RemoteParticipantInterface* GetRemoteParticipantBySid(std::string sid) = 0;
	// LiveKit identity is stable for a participant session and should normally be preferred over
	// the mutable display name.
	RemoteParticipantInterface* GetRemoteParticipantByIdentity(std::string identity);
	virtual RemoteParticipantInterface* GetRemoteParticipantByName(std::string name) = 0;
	virtual std::vector<ParticipantInterface*> Participants() = 0;
	virtual ParticipantInterface* GetParticipantBySid(std::string sid) = 0;
	ParticipantInterface* GetParticipantByIdentity(std::string identity);
	virtual ParticipantInterface* GetParticipantByName(std::string name) = 0;
	// Appended for ABI compatibility with existing RoomInterface implementations.
	virtual bool RegisterTextStreamHandler(std::string, TextStreamHandler) { return false; }
	virtual bool UnregisterTextStreamHandler(const std::string&) { return false; }
	virtual bool RegisterByteStreamHandler(std::string, ByteStreamHandler) { return false; }
	virtual bool UnregisterByteStreamHandler(const std::string&) { return false; }
	// Audio output controls apply to the playback device owned by this room. They are available
	// after Connect() has created the underlying peer transport factory.
	virtual bool SetAudioOutputDevice(std::string) { return false; }
	virtual std::string AudioOutputDevice() const { return {}; }
	virtual bool SetSpeakerVolume(float) { return false; }
	virtual float SpeakerVolume() const { return 1.0F; }
	virtual bool SetSpeakerMuted(bool) { return false; }
	virtual bool SpeakerMuted() const { return false; }
	virtual AudioPlaybackStats GetAudioPlaybackStats() const { return {}; }
	// The returned pointer is owned by the room and remains valid until the room is reconfigured or
	// destroyed. A null pointer means E2EE is not configured.
	virtual E2EEManager* GetE2EEManager() { return nullptr; }
	// Schema definitions are stored by the local participant and queried by stable participant
	// identity. Successful lookups are cached for the lifetime of this room connection.
	virtual DataTrackError StoreDataTrackSchema(DataTrackSchema) {
		return {DataTrackErrorCode::Disconnected, "room is disconnected"};
	}
	virtual DataTrackSchemaResult GetDataTrackSchema(std::string, DataTrackSchemaId) {
		return {{}, {DataTrackErrorCode::Disconnected, "room is disconnected"}};
	}

	// These controls return false when the room is disconnected or the participant/track SID does
	// not belong to the room.
	bool SetLocalTrackMuted(std::string track_sid, bool muted);
	bool SetRemoteTrackSubscribed(std::string participant_sid, std::string track_sid,
	                              bool subscribed);
	bool UpdateRemoteTrackSettings(std::string participant_sid, std::string track_sid,
	                               const RemoteTrackSettings& settings);
};

RoomInterface* CreateRoom();
std::unique_ptr<RoomInterface> CreateRoomUnique(RoomOptions options = default_room_options());

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_ROOM_INTERFACE_H_
